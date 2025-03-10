#include "llvm/Transforms/Utils/LevPA.h"
#include "llvm/Transforms/Utils/SteengaardAnalysis.h"

#include <chrono>
#include <sstream>
#include <string>
#include <iomanip>

using namespace llvm;


static std::string getCurrentTime(){
    const auto Now = std::chrono::system_clock::now();
    const std::time_t TimeNow = std::chrono::system_clock::to_time_t(Now);
    auto ConvertedTime = gmtime(&TimeNow);
    std::stringstream Sstream;
    Sstream << std::put_time(ConvertedTime, "%Y/%m/%d %T");
    auto Millis = std::chrono::duration_cast<std::chrono::milliseconds>(Now.time_since_epoch()) % 1000;

    return "[" + Sstream.str() + "." + std::to_string(Millis.count()) + "]";
}


size_t LevPA::computePointerLevel(size_t PtrId){
    auto Pl = SteengaardResult.getPointerLevel(PtrId);
    return Pl;
}

std::pair<std::map<const Instruction*, std::set<const Instruction*>>, DomGraph> 
LevPA::buildDominatorGraph(const Function *Func, size_t PtrId){

    // todo: Better way to compute IDP?

    DomGraph DG;

    for(auto Node : DefLocations[PtrId][Func]){
        DG.addNode(Node);
    }

    // Compute immediate dominance frontier
    std::set<const Instruction *> CurNodes = DG.getNodes();
    while(!CurNodes.empty()){
        std::set<const Instruction *> NextNodes{};
        for(auto Node : CurNodes){
            auto it = Func2DomFrontier.at(Func).get().find(const_cast<BasicBlock*>(Node->getParent()));
            // If a basicblock is not reachable from the entry basicblock,
            // DominanceFromtierAnalysis will not having entry for this basicblock.
            // i.e., running find will return end ietrator.
            if(it == Func2DomFrontier.at(Func).get().end()){
                CurNodes = std::set<const Instruction *>{};
                break;
            }

            for(auto DF : it->second){
                auto Ns = DG.getNodes();
                if(Ns.find(DF->getFirstNonPHIOrDbg()) == Ns.end()){
                    NextNodes.insert(DF->getFirstNonPHIOrDbg());
                }
                DG.addNode(DF->getFirstNonPHIOrDbg());
                DG.addEdge(Node, DF->getFirstNonPHIOrDbg());
            }
        }
        CurNodes = NextNodes;
    }
    // for any two nodes a b in dg, add a -> b if a dominates b or b in a's df.
    std::map<const Instruction*, std::set<const Instruction*>> Doms;
    auto Nodes = DG.getNodes();
    for(auto Node1 : Nodes){
        for(auto Node2 : Nodes){
            if(Node1 == Node2){
                continue;
            }
            if(Func2DomTree.at(Func).get().dominates(Node1, Node2)){
                Doms[Node2].insert(Node1);
            }
        }
    }
    for(auto P : Doms){
        // Find immediate dominator from all dominators.
        auto IDom = *(P.second.begin());
        for(auto D : P.second){
            if(D == IDom){
                continue;
            }
            if(Func2DomTree.at(Func).get().dominates(IDom, D)){
                IDom = D;
            }
        }
        DG.addEdge(IDom, P.first);

    }


    // Fixed point solution in dominator graph
    std::map<const Instruction*, std::set<const Instruction*>> IN;
    std::map<const Instruction*, std::set<const Instruction*>> OUT;


    while(true){
        bool Changed = false;
        auto Nodes = DG.getNodes();
        auto Edges = DG.getEdges();
        auto Parents = DG.getParents();

        for(auto Node : DG.getNodes()){

            // outs() << "Node: " << *Node << "\n";
            // outs() << *SteengaardResult.getPtr(PtrId).first << " " << Func->getName().str() << "\n";

            // update IN

            IN[Node] = std::set<const ProgramLocationTy*>{};
            for(auto Parent : Parents[Node]){
                IN[Node].insert(OUT[Parent].begin(), OUT[Parent].end());
            }
            // update out
            auto OldOut = OUT[Node];

            if(isa<LoadInst>(Node)){
                OUT[Node] = IN[Node];
            }
            else if(isa<AllocaInst>(Node)){
                IN[Node] = std::set<const ProgramLocationTy*>{Node};
                OUT[Node] = IN[Node];
            }
            else if(isa<CallBase>(Node)){
                OUT[Node] = std::set<const ProgramLocationTy*>{Node};
            }
            else if(auto Store = dyn_cast<StoreInst>(Node)){
                if(isa<GlobalValue>(dyn_cast<StoreInst>(Node)->getPointerOperand()) || isa<GetElementPtrInst>(dyn_cast<StoreInst>(Node)->getPointerOperand())){
                    OUT[Node] = IN[Node];
                    OUT[Node].insert(Node);
                }
                else if(getPointsToSet(SteengaardResult.getID(dyn_cast<StoreInst>(Node)->getPointerOperand(), true), Node).size() <= 1){
                    OUT[Node] = std::set<const ProgramLocationTy*>{Node};
                }
                else{
                    OUT[Node] = IN[Node];
                    OUT[Node].insert(Node);
                }
            }
            else{
                OUT[Node] = IN[Node];
            }

            if(OldOut != OUT[Node]){
                Changed = true;
            }

        }

        if(!Changed){
            break;
        }
    }

    return {OUT, DG};
}


void LevPA::addDefLabel(size_t PtrId, const ProgramLocationTy *Loc){

    DEBUG_WITH_TYPE("fspa", outs() << getCurrentTime() << "add def label " << PtrId << " at " << *Loc << "\n");

    auto IsInserted = LabelMap[Loc].insert(Label(PtrId, Label::LabelType::Def)).second;
    DefLocations[PtrId][Loc->getFunction()].insert(Loc);

    if(!IsInserted){
        return;
    }
    
    auto Ptr = SteengaardResult.getPtr(PtrId).first;
    if(!Ptr){
        return;
    }
    auto PtrLoc = dyn_cast<Instruction>(Ptr);

    // If using a ptr created in other functions.
    if(PtrLoc && PtrLoc->getFunction() != Loc->getFunction()){
        Func2WorkList[Loc->getFunction()][SteengaardResult.getPointerLevel(PtrId)].insert(PtrId);
        auto FirstInst = getFirstInst(Loc->getFunction());
        LabelMap[FirstInst].insert(Label(PtrId, Label::LabelType::Def));
        DefLocations[PtrId][FirstInst->getFunction()].insert(FirstInst);

        // Recursively add use label at all callsite of the current function.
        std::set<const ProgramLocationTy*> WorkList = Func2CallerLocation[Loc->getFunction()];
        while(!WorkList.empty()){
            auto CallSite = *WorkList.begin();
            addDefLabel(PtrId, CallSite);
            WorkList.erase(CallSite);
        }
    }

    return;
}

void LevPA::addUseLabel(size_t PtrId, const ProgramLocationTy *Loc){

    auto IsInserted = LabelMap[Loc].insert(Label(PtrId, Label::LabelType::Use)).second;
    UseList[PtrId].insert(Loc);
    
    if(!IsInserted){
        return;
    }
    auto Ptr = SteengaardResult.getPtr(PtrId).first;
    // outs() << PtrId << "\n";

    if(!Ptr){
        return;
    }

    auto PtrLoc = dyn_cast<Instruction>(Ptr);
    // If using a ptr created in other functions.
    if(PtrLoc && PtrLoc->getFunction() != Loc->getFunction()){
        Func2WorkList[Loc->getFunction()][SteengaardResult.getPointerLevel(PtrId)].insert(PtrId);

        auto FirstInst = getFirstInst(Loc->getFunction());
        LabelMap[FirstInst].insert(Label(PtrId, Label::LabelType::Use));
        UseList[PtrId].insert(FirstInst);

        for(auto Ret : Func2Returns[Loc->getFunction()]){
            LabelMap[Ret].insert(Label(PtrId, Label::LabelType::Use));
            UseList[PtrId].insert(Ret);
        }
        // Recursively add use label at all callsite of the current function.
        std::set<const ProgramLocationTy*> WorkList = Func2CallerLocation[Loc->getFunction()];
        while(!WorkList.empty()){
            auto CallSite = *WorkList.begin();
            addUseLabel(PtrId, CallSite);
            WorkList.erase(CallSite);
        }
    }

    return;
}

std::set<size_t> LevPA::getPointsToSet(size_t PtrId, const ProgramLocationTy *Loc){


    auto PtrIsTopLevel = SteengaardResult.getPtr(PtrId).second;
    auto Ptr = SteengaardResult.getPtr(PtrId).first;

    // outs() << "GPTS: " << *Ptr << " " << *Loc << "\n";


    if(PtrIsTopLevel){
        // pts of top-level variable only defined once.
        if(auto Alloca = dyn_cast<AllocaInst>(Ptr)){
            return PointsToSetOut.at(Alloca).at(PtrId);
        }
        else if(auto Load = dyn_cast<LoadInst>(Ptr)){
            std::set<size_t> res;
            auto DLoc = dyn_cast<Instruction>(SteengaardResult.getPtr(PtrId).first);
            if(!DLoc){
                return res;
            }
            for(auto AliasId : AliasMap[DLoc][PtrId]){
                res.insert(PointsToSetIn.at(DLoc).at(AliasId).begin(), PointsToSetIn.at(DLoc).at(AliasId).end());
            }
            return res;
        }  
        else if(auto Arg = dyn_cast<Argument>(Ptr)){
            auto FirstInst = getFirstInst(Loc->getFunction());
            auto ArgId = SteengaardResult.getID(Arg, true);
            return PointsToSetOut[FirstInst][ArgId];
        }
        else if(auto GEP = dyn_cast<GetElementPtrInst>(Ptr)){
            auto PointerOpId = SteengaardResult.getID(GEP->getPointerOperand(), true);
            return getPointsToSet(PointerOpId, GEP);
        }
        else if(auto BitCast = dyn_cast<BitCastInst>(Ptr)){
            auto PointerOpId = SteengaardResult.getID(BitCast->getOperand(0), true);
            return getPointsToSet(PointerOpId, BitCast);
        }
        else if(auto Call = dyn_cast<CallBase>(Ptr)){
            auto CallId = SteengaardResult.getID(Call, true);
            return PointsToSetOut[Call][CallId];
        }
        else if(auto Global = dyn_cast<GlobalValue>(Ptr)){
            DEBUG_WITH_TYPE("pts", outs() << "Run into global values\n");
            return std::set<size_t>{};
        }
        else if(auto Phi = dyn_cast<PHINode>(Ptr)){
            std::set<size_t> res;
            for(size_t i = 0; i < Phi->getNumIncomingValues(); ++i){
                auto Pts = getPointsToSet(SteengaardResult.getID(Phi->getIncomingValue(i)->stripPointerCastsAndAliases(), true), Phi);
                res.insert(Pts.begin(), Pts.end());
            }
            return res;
        }
        else if(auto IntToPtr = dyn_cast<IntToPtrInst>(Ptr)){
            return std::set<size_t>{};
        }
        else if(auto Null = dyn_cast<Constant>(Ptr)){
            if(Null->isNullValue()){
                auto NullPtrId = SteengaardResult.getID(nullptr, true);
                return std::set<size_t>{NullPtrId};
            }
            return std::set<size_t>{};
        }
        else{
            outs() << *Ptr << "\n";
            llvm_unreachable("toplevel ptr has unknown type");
        }
    }
    else{
        return PointsToSetOut.at(Loc).at(PtrId);
    }
}


const Instruction* LevPA::getFirstInst(const Function *Func){
    return Func->getEntryBlock().getFirstNonPHIOrDbg();
}

const std::set<size_t>& LevPA::getPointersInWorkList(size_t PointerLevel, const Function *Func){
    if(Func2WorkList.count(Func) && Func2WorkList[Func].count(PointerLevel)){
        return Func2WorkList.at(Func).at(PointerLevel);
    }
    static std::set<size_t> empty;
    return empty;
}

///@brief mark labels of pointer \p Ptr at program location \p User.
void LevPA::markLabelsAtUser(const PointerTy *Ptr, size_t PtrId, const User *User){

    if(auto Store = dyn_cast<StoreInst>(User)){
        if(Store->getValueOperand() == Ptr){
            return;
        }
        auto Pts = getPointsToSet(PtrId, Store);
        for(auto PointeeId : Pts){
            addDefLabel(PointeeId, Store);
            addUseLabel(PointeeId, Store);
        }

    }
    else if(auto Load = dyn_cast<LoadInst>(User)){
        auto Pts = getPointsToSet(PtrId, Load);
        for(auto PointeeId : Pts){
            addUseLabel(PointeeId, Load);
        }
    }
    else if(auto Call = dyn_cast<CallBase>(User)){
        // todo: If an allocated top-level variable is directly used as the argument of a call, the points-to set of para is not properly updated.
        // needs verify.
        addDefLabel(PtrId, Call);
        addUseLabel(PtrId, Call);
    }
    else if(auto Return = dyn_cast<ReturnInst>(User)){
        if(Return->getReturnValue()->getType()->isPointerTy()){
            addUseLabel(PtrId, Return);
        }
        
    }
    else if(auto GEP = dyn_cast<GetElementPtrInst>(User)){
        // todo: delegate marking labels for all use of GEP. Be careful with infinite loop.
        for(auto GepUser : GEP->users()){
            markLabelsAtUser(Ptr, PtrId, GepUser);
        }
    }
    else if(auto BitCast = dyn_cast<BitCastInst>(User)){
        // todo: delegate marking labels for all use of BitCast.
        for(auto BitCastUser : BitCast->users()){
            markLabelsAtUser(Ptr, PtrId, BitCastUser);
        }
    }
    else if(auto Phi = dyn_cast<PHINode>(User)){
        addUseLabel(PtrId, Phi);
    }    
    else if(dyn_cast<CmpInst>(User) || dyn_cast<VAArgInst>(User) || dyn_cast<PtrToIntInst>(User) || dyn_cast<SelectInst>(User)){

        DEBUG_WITH_TYPE("warning", outs() << getCurrentTime() << "WARNING:" << *User << " is in the user list of pointer "
            << *Ptr << ", but it's neither storeinst nor loadinst.\n");
    }
    else{
        std::string Str;
        raw_string_ostream(Str) << *User;
        Str = "Cannot process instruction:" + Str + "\n";
        llvm_unreachable(Str.c_str());
    }
}

/// @brief Mark def and use labels for top-level pointer \p Ptr. The labels are later used for building def use graph.
void LevPA::markLabelsForPtr(const PointerTy *Ptr, bool isTopLevel){

    if(!isTopLevel){
        // This function only marks explicit access of the points-to set of a pointer. Since address-taken variables do not
        // have explicit access and we do not consider intermediate variables in the worklist, we ignore them for now. The labels for 
        // address-taken variables will be later marked when propagating points-to sets.
        DEBUG_WITH_TYPE("warning", outs() << getCurrentTime() << "Marking labels for addr-taken " << *Ptr << " " << isTopLevel << "\n");
        return; 
    }

    DEBUG_WITH_TYPE("fspa", outs() << getCurrentTime() << " Marking labels for " << *Ptr << "\n");

    auto PtrId = SteengaardResult.getID(Ptr, true);
    for(auto User : Ptr->users()){
        markLabelsAtUser(Ptr, PtrId, User);
    }
}


std::set<const LevPA::ProgramLocationTy*> LevPA::getUseLocations(size_t PtrId){
    if(UseList.count(PtrId)){
        return UseList.at(PtrId);
    }
    return std::set<const ProgramLocationTy*>{};
}

void LevPA::buildDefUseGraph(std::set<const ProgramLocationTy*> UseLocs, 
    size_t PtrId, std::map<const Instruction*, std::set<const Instruction*>> OUT, DomGraph DG){
    for(auto UseLoc : UseLocs){
        DEBUG_WITH_TYPE("fspa", outs() << getCurrentTime() << " Building def-use graph for " << PtrId << " at " << *UseLoc << "\n");

        // Find all def in dominator graph that dominates useLoc
        auto Nodes = DG.getNodes();
        std::set<const ProgramLocationTy *> Dom{};
        for(auto Node : Nodes){
            if(Func2DomTree.at(UseLoc->getFunction()).get().dominates(Node, UseLoc)){
                Dom.insert(Node);
            }
        }
        if(Dom.empty()){
            continue;
        }

        // Find immediate dominator
        auto IDom = *(Dom.begin());
        for(auto D : Dom){
            if(D == IDom){
                continue;
            }
            if(Func2DomTree.at(UseLoc->getFunction()).get().dominates(IDom, D)){
                IDom = D;
            }
        }

        // Out[idom] are the defs
        DEBUG_WITH_TYPE("fspa", outs() << getCurrentTime() << " Found immediate dominator " << *IDom << " for " << *UseLoc << "\n");

        auto DefLocs = OUT[IDom];
        auto it0 = Nodes.find(UseLoc);
        auto it1 = DefLocations[PtrId][UseLoc->getFunction()].find(UseLoc);
        if(it0 != Nodes.end() && it1 == DefLocations[PtrId][UseLoc->getFunction()].end()){
            DefLocs = OUT[UseLoc];
        }

        DEBUG_WITH_TYPE("fspa", outs() << getCurrentTime() << " Found " << DefLocs.size() << " def locations of pointer " << PtrId << " at " << *UseLoc << "\n");

        for(auto Def : DefLocs){
            addDefUseEdge(Def, UseLoc, PtrId);
        }
    }
}

void LevPA::addDefUseEdge(const ProgramLocationTy *Def, const ProgramLocationTy *Use, size_t PtrId){

    DEBUG_WITH_TYPE("dug", outs() << getCurrentTime() << " Add def Use edge " << *Def << " === " << PtrId << " ===> " << *Use << "\n");
    DefUseGraph[Def][PtrId].insert(Use);
    UseDefGraph[Use][PtrId].insert(Def);

}


void LevPA::globalInitialize(Module &M){
    for(auto &Func : M.functions()){
        initialize(&Func);
    }
}

void LevPA::initialize(const Function *Func){

    DEBUG_WITH_TYPE("fspa", outs() << getCurrentTime() << " Initializing function " << Func->getName() << "\n");
    WorkListTy WorkList;

    // Initialize function parameters
    if(!Func->isDeclaration()){
        auto FirstInst = getFirstInst(Func);
        for(const auto &Arg : Func->args()){
            if(!Arg.getType()->isPointerTy()){
                continue;
            }
            auto ArgId = SteengaardResult.getID(&Arg, true);
            addDefLabel(ArgId, FirstInst);
            PointsToSetOut[FirstInst][ArgId] = std::set<size_t>{};
            WorkList[computePointerLevel(ArgId)].insert(ArgId);
        }
    }

    auto NullPtrId = SteengaardResult.getID(nullptr, true);
    for(auto &Inst : instructions(*Func)){
        if(const AllocaInst *Alloca = dyn_cast<AllocaInst>(&Inst)){
            auto AllocaAddrTakenId = SteengaardResult.getID(Alloca, false);
            WorkList[computePointerLevel(AllocaAddrTakenId)].insert(AllocaAddrTakenId);
            addDefLabel(AllocaAddrTakenId, Alloca);
            // A -> nullptr means A is not initialized. It helps us to find dereference of nullptr.
            // todo: add nullptr
            PointsToSetIn[&Inst][AllocaAddrTakenId] = std::set<size_t>{NullPtrId};
            PointsToSetOut[&Inst][AllocaAddrTakenId] = std::set<size_t>{NullPtrId};
            
            auto AllocaTopLevelId = SteengaardResult.getID(Alloca, true);
            WorkList[computePointerLevel(AllocaTopLevelId)].insert(AllocaTopLevelId);
            addDefLabel(AllocaTopLevelId, Alloca);
            PointsToSetIn[&Inst][AllocaTopLevelId] = std::set<size_t>{AllocaAddrTakenId};
            PointsToSetOut[&Inst][AllocaTopLevelId] = std::set<size_t>{AllocaAddrTakenId};
            
        }
        else if(const auto Call = dyn_cast<CallBase>(&Inst)){
            Func2CallerLocation[Call->getCalledFunction()].insert(Call);
            if(!Call->getCalledFunction() || Call->getCalledFunction()->isDeclaration() || Call->getFunctionType()->isVarArg()){
                DEBUG_WITH_TYPE("warning", outs() << getCurrentTime() << " WARNING:" << *Call << " performs an indirect call\n");
                continue;
            }
            else{
                // Caller2Callee[Func].insert(Call->getCalledFunction());
                for(size_t Idx = 0; Idx < Call->arg_size(); ++Idx){
                    auto Arg = Call->getArgOperand(Idx);
                    if(Arg && Arg->getType()->isPointerTy()){
                        CallSite2ArgIdx[Call][SteengaardResult.getID(Arg, true)].insert(Idx);
                    }
                }
            }          
        }
        else if(const ReturnInst *Return = dyn_cast<ReturnInst>(&Inst)){
            Func2Returns[Func].insert(Return);
        }
    }

    Func2WorkList.emplace(Func, WorkList);

}

std::set<size_t> LevPA::getLevPaPts(size_t PointerId){
    if(LevPaPts.count(PointerId)){
        return LevPaPts.at(PointerId);
    }

    return std::set<size_t>{};
}

std::set<const Instruction*> LevPA::getDefinitionLocs(size_t PointerId, const Instruction *Loc){
    if(UseDefGraph[Loc].count(PointerId)){
        return UseDefGraph[Loc].at(PointerId);
    }

    return std::set<const Instruction*>{};
}


/// @brief get the set of indexes that uniquely identify the new variable introduced to represent the version of \p Pointer from all program locations that defines \p Loc
std::set<size_t> LevPA::getLastVersion(size_t PointerId, const Instruction *Loc){
    std::set<size_t> res;
    auto DefLocs = getDefinitionLocs(PointerId, Loc);
    for(auto dl : DefLocs){
        auto Pid = getCurrentVersion(PointerId, dl);
        res.insert(Pid);
    }

    return res;
}

size_t LevPA::getCurrentVersion(size_t PointerId, const Instruction *Loc){
    std::set<size_t> res;

    if(!AdditionalPointerIdMap[Loc].count(PointerId)){
        createNewVersionOfPointer(PointerId, Loc);
    }
    return AdditionalPointerIdMap[Loc].at(PointerId);
}

void LevPA::createNewVersionOfPointer(size_t PointerId, const Instruction *Loc){
    AdditionalPointerIdMap[Loc][PointerId] = index++;
    return;
}

void LevPA::createCopyRule(size_t Lhs, size_t Rhs, size_t pl){
    PointerLevelToConstraints[pl].insert({Lhs, Rhs, true});
}

void LevPA::createAllocaRule(size_t TopLvlId, size_t AddrTakenId, size_t pl){
    PointerLevelToConstraints[pl].insert({TopLvlId, AddrTakenId, false});
}
    
void LevPA::createStrongUpdateRule(size_t CurrentVersion, size_t Pointer, size_t pl){
    PointerLevelToConstraints[pl].insert({CurrentVersion, Pointer, true});
}

void LevPA::createWeakUpdateRule(size_t CurrentVersion, std::set<size_t> LastVersions, size_t ValueOpId, size_t pl){
    for(auto lv : LastVersions){
        PointerLevelToConstraints[pl].insert({CurrentVersion, lv, true});
    }
    PointerLevelToConstraints[pl].insert({CurrentVersion, ValueOpId, true});


}

void LevPA::solveConstraints(size_t CurrentPointerLevel){
    auto cons = PointerLevelToConstraints[CurrentPointerLevel];

    std::map<size_t, std::set<size_t>> CEdges;


    for(auto [lhs, rhs, isCopy] : cons){

        if(isCopy){
            CEdges[rhs].insert(lhs);
        }
        else{
            LevPaPts[lhs].insert(rhs);
        }
    }

    bool isChanged = true;
    while(isChanged){
        isChanged = false;
        for(auto [from,toSet] : CEdges){
            for(auto to : toSet){
                for(auto e : LevPaPts[to]){
                    isChanged = isChanged || LevPaPts[from].insert(e).second;
                }
            }
        }
    }
}


void LevPA::markLabelsforNextPointerLevel(size_t CurrentPointerLevel){
    auto Pointers = SteengaardResult.getPointersInPointerLevel(CurrentPointerLevel);
    for(auto PointerId : Pointers){
        auto Pointer = SteengaardResult.getPtr(PointerId).first;

        for(auto User : Pointer->users()){
            if(auto Store = dyn_cast<StoreInst>(User)){
                if(Pointer != Store->getPointerOperand()){
                    continue;
                }
                for(auto p : LevPaPts[PointerId]){
                    addDefLabel(p, Store);
                    addUseLabel(p, Store);
                }
            }

            if(auto Load = dyn_cast<LoadInst>(User)){
                for(auto p : LevPaPts[PointerId]){
                    addUseLabel(p, Load);
                }
            }
        }
    }

    return;

}






LevPaResult LevPA::run(Module &m, ModuleAnalysisManager &mam){

    SteengaardResult = mam.getResult<SteengaardAnalysis>(m);
    index = SteengaardResult.getIndex();



    auto start = std::chrono::high_resolution_clock::now();
    auto CurrentPointerLevel = SteengaardResult.getMaxPl();
    globalInitialize(m);


    auto &FAM = mam.getResult<FunctionAnalysisManagerModuleProxy>(m).getManager();
    
    while(CurrentPointerLevel > 0){
        for(auto &Func : m.functions()){
            if(Func.isDeclaration()){
                continue;
            }

            Func2DomTree.emplace(&Func, FAM.getResult<DominatorTreeAnalysis>(Func));
            Func2DomFrontier.emplace(&Func, FAM.getResult<DominanceFrontierAnalysis>(Func));

            auto Pointers = getPointersInWorkList(CurrentPointerLevel, &Func);
            for(auto PtrId : Pointers){
                const auto& [Ptr, IsTopLevel] = SteengaardResult.getPtr(PtrId);
                markLabelsForPtr(Ptr, IsTopLevel);
            }
        }

        for(auto &Func : m.functions()){
            auto Pointers = getPointersInWorkList(CurrentPointerLevel, &Func);
            for(auto PtrId : Pointers){
                const auto& [Out, DG] = buildDominatorGraph(&Func, PtrId);
                buildDefUseGraph(getUseLocations(PtrId), PtrId, Out, DG);
            }
        }

        // outs() << "111111111\n";

        /*
            Build dug
            Collect Andersen constraints
            Solve constraints
            Leave labels for next level.
        */
        for(auto &Func : m.functions()){
            auto Pointers = SteengaardResult.getPointersInPointerLevel(CurrentPointerLevel);
            
            for(auto PointerId : Pointers){

                auto Pointer = SteengaardResult.getPtr(PointerId).first;

                // outs() << "Pointer " << *Pointer << "\n";
                if(!SteengaardResult.getPtr(PointerId).second){
                    continue;
                }

                // we are not looking for users for load, but process load itself.
                if(auto Load = dyn_cast<LoadInst>(Pointer)){
                    auto PointerOpId = SteengaardResult.getID(Load->getPointerOperand(), true);
                    auto LoadId = SteengaardResult.getID(Load, true);
                    auto Pts = getLevPaPts(PointerOpId);
                    for(auto PointeeId : Pts){
                        for(auto lv : getLastVersion(PointeeId, Load)){
                            createCopyRule(LoadId, lv, CurrentPointerLevel);
                        }
                        
                    }
                }
                else if(auto Alloca = dyn_cast<AllocaInst>(Pointer)){
                    auto TopLvlId = SteengaardResult.getID(Alloca, true);
                    auto AddrTakenId = SteengaardResult.getID(Alloca, false);
                    createAllocaRule(TopLvlId, AddrTakenId, CurrentPointerLevel);
                }

                for(auto Usr : Pointer->users()){
                    // outs() << "User " << *Usr << "\n";

                    if(auto Store = dyn_cast<StoreInst>(Usr)){
                        if(Pointer == Store->getPointerOperand()){
                            continue;
                        }

                        auto Pts = getLevPaPts(SteengaardResult.getID(Store->getPointerOperand(), true));
                        if(Pts.size() <= 1){
                            for(auto Pointee : Pts){
                                createStrongUpdateRule(getCurrentVersion(Pointee, Store), PointerId, CurrentPointerLevel);
                            }
                        }
                        else{
                            for(auto Pointee : Pts){
                                createWeakUpdateRule(getCurrentVersion(Pointee, Store), getLastVersion(Pointee, Store), PointerId, CurrentPointerLevel);
                            }
                        }
                    }
                    else if(auto BitCast = dyn_cast<BitCastInst>(Usr)){
                        auto BitCastId = SteengaardResult.getID(BitCast, true);
                        createCopyRule(BitCastId, PointerId, CurrentPointerLevel);
                    }
                    else if(auto GEP = dyn_cast<GetElementPtrInst>(Usr)){
                        if(Pointer != GEP->getPointerOperand()){
                            continue;
                        }
                        auto GEPId = SteengaardResult.getID(GEP, true);
                        createCopyRule(GEPId, PointerId, CurrentPointerLevel);
                    }
                    else if(auto Call = dyn_cast<CallBase>(Usr)){

                        // outs() << Call->getCalledFunction() << " " << Call->getCalledFunction()->isDeclaration() << "\n";

                        if(!Call->getCalledFunction() || Call->getCalledFunction()->isDeclaration() || Call->getFunctionType()->isVarArg()){
                            continue;
                        }

                        size_t ArgIdx = 0;
                        while(ArgIdx < Call->arg_size() && ArgIdx < Call->getCalledFunction()->arg_size()){
                            if(Call->getArgOperand(ArgIdx) == Pointer){
                                break;
                            }
                            ++ArgIdx;
                        }
                        if(ArgIdx < Call->arg_size() && ArgIdx < Call->getCalledFunction()->arg_size()){
                            createCopyRule(SteengaardResult.getID(Call->getCalledFunction()->getArg(ArgIdx), true), PointerId, CurrentPointerLevel);
                        }
                    }
                    else if(auto Return = dyn_cast<ReturnInst>(Usr)){
                        for(auto CallSite : Func2CallerLocation[Return->getFunction()]){
                            createCopyRule(SteengaardResult.getID(CallSite, true), PointerId, CurrentPointerLevel);
                        }
                    }
                }
            }

            solveConstraints(CurrentPointerLevel);

            markLabelsforNextPointerLevel(CurrentPointerLevel);

        }


        // outs() << "222222222\n";

        --CurrentPointerLevel;

    }

    auto stop = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(stop - start);

    outs() << "Runtime: " << duration.count() << "ms\n";

    return AnalysisResult;

}



AnalysisKey LevPA::Key;

#include "llvm/Transforms/Utils/FlowSensitivePointerAnalysis.h"


#include "llvm/Support/Debug.h"
#include "llvm/Support/Timer.h"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <fstream>


// 1. some def use edge are found more than once. DONE
// 2. temporary variables should not be propagated. DONE
// 3. only handle para of ptr type. DONE
// 5. Add support for global variable.

using namespace llvm;


/// @brief Get current system time as a string
static std::string getCurrentTime(){
    const auto Now = std::chrono::system_clock::now();
    const std::time_t TimeNow = std::chrono::system_clock::to_time_t(Now);
    auto ConvertedTime = gmtime(&TimeNow);
    std::stringstream Sstream;
    Sstream << std::put_time(ConvertedTime, "%Y/%m/%d %T");
    auto Millis = std::chrono::duration_cast<std::chrono::milliseconds>(Now.time_since_epoch()) % 1000;

    return "[" + Sstream.str() + "." + std::to_string(Millis.count()) + "]";
}

/// @brief Print the points-to set for \p Ptr at program location \p Loc when 
///     running in debug mode. 
void FlowSensitivePointerAnalysis::printPointsToSetAtProgramLocation(const ProgramLocationTy *Loc){

    if(PointsToSetOut.count(Loc)){
        outs() << "At program location" << *Loc << ":\n";
        for(auto PtsForPtr : PointsToSetOut.at(Loc)){
            auto Ptr = SteengaardResult.getPtr(PtsForPtr.first);
            std::string PtrType = Ptr.second ? "(TopLevel)" : "(AddrTaken)";
            if(!Ptr.first){
                outs() << "\t " << "nullptr ==>" << "\n";
            }
            else if(dyn_cast<Argument>(Ptr.first)){
                outs() << "\t" << *(Ptr.first) << " " << PtrType << " ==>\n";
            }
            else{
                outs() << *(Ptr.first) << " " << PtrType << " ==>\n";
            }
            
            for(auto PointeeId : PtsForPtr.second){
                auto Pointee = SteengaardResult.getPtr(PointeeId);
                std::string PtrType = Pointee.second ? "(TopLevel)" : "(AddrTaken)";
                if(!Pointee.first){
                    outs() << "\t " << "nullptr" << "\n";
                }
                else if(dyn_cast<Instruction>(Pointee.first)){
                    outs() << "\t" << *(Pointee.first) << " " << PtrType << "\n";
                }
                else{
                    outs() << "\t " << *(Pointee.first) << " " << PtrType << "\n";
                }
            }
        }
    }

}

void FlowSensitivePointerAnalysis::dumpPointsToSet(){
    outs() << "Print points-to set stats\n";
    // C++26 will treat _ as a special value that does not cause unused warning.
    for(auto PtsForPtr : PointsToSetOut){
        printPointsToSetAtProgramLocation(PtsForPtr.first);
    }
}

void FlowSensitivePointerAnalysis::dumpPointsToSetIn(){
    outs() << "Print points-to set in stats\n";
    // C++26 will treat _ as a special value that does not cause unused warning.
    for(auto PtsForPtr : PointsToSetIn){
        // printPointsToSetAtProgramLocation(PtsForPtr.first);

        if(PointsToSetIn.count(PtsForPtr.first)){
            outs() << "At program location" << *PtsForPtr.first << ":\n";
            for(auto PtsForPtr : PointsToSetIn.at(PtsForPtr.first)){
                auto Ptr = SteengaardResult.getPtr(PtsForPtr.first);
                std::string PtrType = Ptr.second ? "(TopLevel)" : "(AddrTaken)";
                if(!Ptr.first){
                    outs() << "\tnullptr " << PtrType << " ==>\n";
                }
                else if(dyn_cast<Argument>(Ptr.first)){
                    outs() << "\t" << *(Ptr.first) << " " << PtrType << " ==>\n";
                }
                else{
                    outs() << *(Ptr.first) << " " << PtrType << " ==>\n";
                }
                
                for(auto PointeeId : PtsForPtr.second){
                    auto Pointee = SteengaardResult.getPtr(PointeeId);
                    std::string PtrType = Pointee.second ? "(TopLevel)" : "(AddrTaken)";
                    if(!Pointee.first){
                        outs() << "\t " << "nullptr" << "\n";
                    }
                    else if(dyn_cast<Instruction>(Pointee.first)){
                        outs() << "\t" << *(Pointee.first) << " " << PtrType << "\n";
                    }
                    else{
                        outs() << "\t " << *(Pointee.first) << " " << PtrType << "\n";
                    }
                }
            }
        }
    }
}

void FlowSensitivePointerAnalysis::dumpAliasMap(){
    DEBUG_WITH_TYPE("fspa", outs() << "Print alias map stats\n");
    // C++26 will treat _ as a special value that does not cause unused warning.
    for(auto LocAndPtr : AliasMap){
           if(PointsToSetOut.count(LocAndPtr.first)){
            DEBUG_WITH_TYPE("fspa", outs() << "At program location" << *LocAndPtr.first << ":\n");
            for(auto AliasForPtr : AliasMap.at(LocAndPtr.first)){
                auto Ptr = SteengaardResult.getPtr(AliasForPtr.first).first;
                std::string PtrType = SteengaardResult.getPtr(AliasForPtr.first).second ? "(TopLevel)" : "(AddrTaken)";

                DEBUG_WITH_TYPE("fspa", outs() << *Ptr << " " << PtrType << " alias to \n");
                for(auto PointeeId : AliasForPtr.second){
                    auto Pointee = SteengaardResult.getPtr(PointeeId).first;
                    std::string PtrType = SteengaardResult.getPtr(PointeeId).second ? "(TopLevel)" : "(AddrTaken)";

                    if(!Pointee){
                        DEBUG_WITH_TYPE("fspa", outs() << "\t " << "nullptr" << "\n");
                    }
                    else if(dyn_cast<Instruction>(Pointee)){
                        DEBUG_WITH_TYPE("fspa", outs() << "\t" << *Pointee << " " << PtrType << "\n");
                    }
                    else{
                        DEBUG_WITH_TYPE("fspa", outs() << "\t " << *Pointee << " " << PtrType << "\n");
                    }
                }
            }
        }

    }
}

void FlowSensitivePointerAnalysis::dumpLabelMap(){

    outs() << "Print label map\n";
    for(auto p : LabelMap){
        outs() << "Labels at" << *p.first << "\n";
        for(auto e : p.second){
            outs() << "\t" << e << "\n";
        }
    }

}

double FlowSensitivePointerAnalysis::computeAvgPtsSize(){
    size_t TotalPtsSize = 0, NumPts = 0, MaxPtsSize = 0;
    for(auto Pair : PointsToSetOut){
        for(auto P : Pair.second){
            if(!SteengaardResult.getPtr(P.first).second){
                if(P.second.size() > MaxPtsSize){
                    MaxPtsSize = P.second.size();
                }
                TotalPtsSize += P.second.size();
                NumPts += 1;    
            }
            
        }
    }
    std::cout << "End of analysis. Avg Pts Size is " << std::setprecision(5) << (double)TotalPtsSize / NumPts << "\n";
    std::cout << "Max Pts size is: " << MaxPtsSize << "\n";
    return (double)TotalPtsSize / NumPts;
}

void FlowSensitivePointerAnalysis::dumpWorkList(){
    outs() << "Worklist\n";
    for(auto p : Func2WorkList){
        outs() << "Function " << p.first->getName().str() << "\n";
        for(auto wl : p.second){
            outs() << "Pointer level: " << wl.first << "\n";
            for(auto e : wl.second){
                outs() << "\t" << e << "\n";
            }
        }
        
    }
}

void FlowSensitivePointerAnalysis::dumpDefUseGraph(){
    outs() << "DUG\n";
    for(auto edge : DefUseGraph){
        for(auto p : edge.second){
            for(auto p0 : p.second){
                outs() << *edge.first << " == " << p.first << " ==> " << *p0 << "\n";
            } 
        }
    }
}

/// @brief Initialize analysis for all functions in current module. 
void FlowSensitivePointerAnalysis::globalInitialize(Module &M){
    for(auto &Func : M.functions()){
        initialize(&Func);
    }
}

/// @brief Compute the pointer level of an allocated pointer.
/// @return Pointer level for \p Ptr.
size_t FlowSensitivePointerAnalysis::computePointerLevel(size_t PtrId){
    auto Pl = SteengaardResult.getPointerLevel(PtrId);
    return Pl;
}

void FlowSensitivePointerAnalysis::addDefLabel(size_t PtrId, const ProgramLocationTy *Loc){

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

void FlowSensitivePointerAnalysis::addUseLabel(size_t PtrId, const ProgramLocationTy *Loc){

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

const Instruction* FlowSensitivePointerAnalysis::getFirstInst(const Function *Func){
    return Func->getEntryBlock().getFirstNonPHIOrDbg();
}

/// @brief Calculate pointer level for function \p Func. Mark labels for each pointer
///     related instructions. Store pointers into worklist according to their pointer level.
void FlowSensitivePointerAnalysis::initialize(const Function *Func){

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

std::set<size_t> FlowSensitivePointerAnalysis::getPointsToSet(size_t PtrId, const ProgramLocationTy *Loc){


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
        else if(auto Null = dyn_cast<Constant>(Ptr)){
            if(Null->isNullValue()){
                auto NullPtrId = SteengaardResult.getID(nullptr, true);
                return std::set<size_t>{NullPtrId};
            }
            llvm_unreachable("toplevel ptr is constant but not null");
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
        else{
            outs() << *Ptr << "\n";
            llvm_unreachable("toplevel ptr has unknown type");
        }
    }
    else{
        return PointsToSetOut.at(Loc).at(PtrId);
    }
}

///@brief mark labels of pointer \p Ptr at program location \p User.
void FlowSensitivePointerAnalysis::markLabelsAtUser(const PointerTy *Ptr, size_t PtrId, const User *User){

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
    else if(dyn_cast<CmpInst>(User) || dyn_cast<VAArgInst>(User) || dyn_cast<PtrToIntInst>(User)){

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
void FlowSensitivePointerAnalysis::markLabelsForPtr(const PointerTy *Ptr, bool isTopLevel){

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

/// @brief Get all program locations that use pointer \p Ptr
std::set<const FlowSensitivePointerAnalysis::ProgramLocationTy*> FlowSensitivePointerAnalysis::getUseLocations(size_t PtrId){
    if(UseList.count(PtrId)){
        return UseList.at(PtrId);
    }
    return std::set<const ProgramLocationTy*>{};
}

/// @brief Add def use graph for pointer \p Ptr.
void FlowSensitivePointerAnalysis::addDefUseEdge(const ProgramLocationTy *Def, const ProgramLocationTy *Use, size_t PtrId){

    DEBUG_WITH_TYPE("dug", outs() << getCurrentTime() << " Add def Use edge " << *Def << " === " << PtrId << " ===> " << *Use << "\n");
    DefUseGraph[Def][PtrId].insert(Use);
}

/// @brief Create and insert def use edge for pointer \p Ptr.
void FlowSensitivePointerAnalysis::buildDefUseGraph(std::set<const ProgramLocationTy*> UseLocs, 
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

/// @brief Collect all use locations that reachable from a def location by tracing
///     pointer \p Ptr.
std::vector<const FlowSensitivePointerAnalysis::ProgramLocationTy*> FlowSensitivePointerAnalysis::
    getAffectUseLocations(const ProgramLocationTy *Loc, size_t PtrId){

    // outs() << "GAUL: " << *Loc << " " << PtrId << "\n";

    std::vector<const ProgramLocationTy*> Res{};
    if(DefUseGraph.count(Loc)){
        for(auto UseLocsAndPtr : DefUseGraph.at(Loc)){
            if(PtrId == UseLocsAndPtr.first){
                Res.insert(Res.begin(), UseLocsAndPtr.second.begin(), UseLocsAndPtr.second.end());
            }
        }
    }

    DEBUG_WITH_TYPE("pts", outs() << getCurrentTime() << " Got " << Res.size() 
        << " affect use locations for " << PtrId << " at " << *Loc << "\n");        
    return Res;
}

/// @brief Find all def use edges starting from the allocation location of \p PointerIds in function \p Func.
///     When we start propagating points-to information, we want to start with these edges.
SetVector<FlowSensitivePointerAnalysis::DefUseEdgeTupleTy> FlowSensitivePointerAnalysis::
    initializePropagateList(std::set<size_t> PointerIds, size_t PtrLvl, const Function *Func){

    SetVector<DefUseEdgeTupleTy> PropagateList{};
    for(auto PtrId: PointerIds){
        auto Ptr = SteengaardResult.getPtr(PtrId).first;
        if(!Func->isDeclaration()){
            if(auto Arg = dyn_cast<Argument>(Ptr)){
                auto FirstInst = getFirstInst(Func);
                for(auto UseLoc : getAffectUseLocations(FirstInst, PtrId)){
                    PropagateList.insert(std::make_tuple(FirstInst, UseLoc, PtrId));
                }
            }
            else if(auto Loc = dyn_cast<ProgramLocationTy>(Ptr)){
                auto InitialDUEdges = getAffectUseLocations(Loc, PtrId);
                for(auto UseLoc : InitialDUEdges){
                    PropagateList.insert(std::make_tuple(Loc, UseLoc, PtrId));
                }
            }
        } 
    }
    return PropagateList;
}

/// @brief Pass PTS-OUT(Ptr) from DefLoc to  PTS-IN(Ptr) at UseLoc.
void FlowSensitivePointerAnalysis::propagatePointsToInformation(const ProgramLocationTy *UseLoc,
     const ProgramLocationTy *DefLoc, size_t PtrId){

        // Create pts PointsToSetIn[UseLoc][PtrId] to avoid later error raised by calling at() on PointsToSetIn.
        PointsToSetIn[UseLoc][PtrId];
        for(auto PointerId : PointsToSetOut.at(DefLoc).at(PtrId)){
            auto Pointer = SteengaardResult.getPtr(PointerId).first;
            if(!isa_and_nonnull<LoadInst>(Pointer)){
                PointsToSetIn[UseLoc][PtrId].insert(PointerId);
            }
        }

    return;
}

/// @brief Update points-to-set for \p Ptr at program location \p Loc.
/// @return True if the points-to set is changed.
bool FlowSensitivePointerAnalysis::updatePointsToSetAtProgramLocation(const ProgramLocationTy *Loc, 
    size_t PtrId, std::set<size_t> &PTS){

    auto OldPTS = std::set<size_t>{};
    if(PointsToSetOut.count(Loc) && PointsToSetOut[Loc].count(PtrId)){
        OldPTS = PointsToSetOut.at(Loc).at(PtrId);
    }

    
    if(OldPTS != PTS){
        PointsToSetOut[Loc][PtrId] = PTS;
        return true;
    }
    return false;
}

bool FlowSensitivePointerAnalysis::insertPointsToSetAtProgramLocation(const ProgramLocationTy *Loc, 
    size_t PtrId, std::set<size_t> &PTS){
    
        bool Changed = false;
        for(auto Pointer : PTS){
            if(PointsToSetOut[Loc][PtrId].insert(Pointer).second){
                Changed = true;
            }
        }

        return Changed;
}

/// @brief Perform either strong update or weak update for \p Pointer at \p Loc
///     according to the size of aliases of \p Pointer. Add def-use edges to
///     \p PropagateList if needed.
void FlowSensitivePointerAnalysis::updatePointsToSet(const ProgramLocationTy *Loc,
     size_t PointerId, std::set<size_t> AdjustedPointsToSet, SetVector<DefUseEdgeTupleTy> &PropagateList){

    auto Store = dyn_cast<StoreInst>(Loc);
    auto PointerOpId = SteengaardResult.getID(Store->getPointerOperand()->stripPointerCastsAndAliases(), true);
    auto AliasSet = getPointsToSet(PointerOpId, Store);
    
    if(AliasSet.size() <= 1){
        // Strong update
        if(updatePointsToSetAtProgramLocation(Loc, PointerId, AdjustedPointsToSet)){
            for(auto UseLoc : getAffectUseLocations(Loc, PointerId)){   
                // if(Loc != UseLoc){
                    PropagateList.insert(std::make_tuple(Loc, UseLoc, PointerId));
                // } 
            }
        }
    }
    else{
        // Weak update
        for(auto AliasId : AliasSet){
            auto Alias = SteengaardResult.getPtr(AliasId).first;
            if(!Alias || dyn_cast<LoadInst>(Alias)){
                continue;
            }

            PointsToSetOut[Loc][AliasId] = PointsToSetIn[Loc][AliasId];
            if(insertPointsToSetAtProgramLocation(Loc, AliasId, AdjustedPointsToSet)){
                for(auto UseLoc : getAffectUseLocations(Loc, AliasId)){    
                    // if(Loc != UseLoc){
                        PropagateList.insert(std::make_tuple(Loc, UseLoc, AliasId));
                    // }
                }
            }
        }
    }

    
}

/// @brief Update the alias set of pointer x introduced by a \p loadInst 'x = load y' or 'store y x' using pts(PtrId).
void FlowSensitivePointerAnalysis::updateAliasInformation(const ProgramLocationTy *Loc, size_t AliasId, size_t PtrId){
    // dumpPointsToSet();

    auto Pts = getPointsToSet(PtrId, Loc);
    AliasMap[Loc][AliasId].insert(Pts.begin(), Pts.end());
    
    return;
}

/// @brief Propagate the alias set of \p Loc at \p Loc to its use locations.
///     Update its user accordingly.
void FlowSensitivePointerAnalysis::updateAliasUsers(const Value *Alias, size_t PtrId, SetVector<DefUseEdgeTupleTy> &PropagateList){

    const ProgramLocationTy *Loc;
    if(isa<LoadInst>(Alias)){
        Loc = dyn_cast<LoadInst>(Alias);
    }
    else if(isa<Argument>(Alias)){
        Loc = getFirstInst(dyn_cast<Argument>(Alias)->getParent());
    }
    //todo: add gep.
    else if(auto BitCast = dyn_cast<BitCastInst>(Alias)){
        auto OriginalPointer = BitCast->stripPointerCastsAndAliases();
        if(isa<LoadInst>(OriginalPointer)){
            Loc = dyn_cast<LoadInst>(OriginalPointer);
        }
        else if(isa<Argument>(OriginalPointer)){
            Loc = getFirstInst(dyn_cast<Argument>(OriginalPointer)->getParent());
        }
        else if(isa<CallBase>(OriginalPointer)){
            Loc = dyn_cast<CallBase>(OriginalPointer);
        }
    }
    else if(auto Call = dyn_cast<CallBase>(Alias)){
        Loc = dyn_cast<CallBase>(Alias);
    }

    
    for(auto User : Alias->users()){      

        DEBUG_WITH_TYPE("fspa", outs() << getCurrentTime() << " Updating alias user for pointer " << *Alias << " at " << *User << " with id " << PtrId << "\n");  


        auto UseLoc = dyn_cast<Instruction>(User);
        auto Ptr = dyn_cast<PointerTy>(Alias);
        auto LoadId = SteengaardResult.getID(Ptr, true);

        if(AliasMap.count(Loc) && AliasMap[Loc].count(LoadId)){
            AliasMap[UseLoc][LoadId] = AliasMap.at(Loc).at(LoadId);
        }else{
            AliasMap[UseLoc][LoadId] = std::set<size_t>{};
        }
        
        if(auto Store = dyn_cast<StoreInst>(UseLoc)){
            if(!Store->getValueOperand()->getType()->isPointerTy()){
                continue;
            }
            if(Ptr == Store->getPointerOperand()){ 
                if(!Store->getValueOperand()->getType()->isPointerTy()){
                    continue;
                }

                auto PointerOpId = SteengaardResult.getID(dyn_cast<LoadInst>(Loc), true);
                for(auto Pid : getPointsToSet(PointerOpId, Loc)){
                    addDefLabel(Pid, UseLoc);
                    addUseLabel(Pid, UseLoc);
                }
                
            }
            else if(Ptr == Store->getValueOperand()){

                auto Pts = getPointsToSet(LoadId, Loc);

                auto LoadId = SteengaardResult.getID(Ptr, true);
                if(isAlias(LoadId, PtrId, Store)){
                    auto PointerOpId = SteengaardResult.getID(Store->getPointerOperand()->stripPointerCastsAndAliases(), true);

                    if(isa<LoadInst>(Store->getPointerOperand()->stripPointerCastsAndAliases())){

                        auto Location = dyn_cast<Instruction>(Store->getPointerOperand()->stripPointerCastsAndAliases());
                        for(auto Alias : AliasMap[Store][PointerOpId]){
                            for(auto Pe : getPointsToSet(Alias, Location)){
                                updatePointsToSet(Store, Pe, Pts, PropagateList);
                            } 
                        }

                    }
                    else{
                        for(auto Pe : getPointsToSet(PointerOpId, Store)){
                            updatePointsToSet(Store, Pe, Pts, PropagateList);
                        }

                    }
                }
                // else{
                //     outs() << LoadId << " not alias to " << PtrId << "\n";
                // }
                // printPointsToSetAtProgramLocation(UseLoc);
            }
            else{
                std::string Str;
                raw_string_ostream(Str) << "Hitting at " << *Store << " with pointer " << *Loc << "\n";
                llvm_unreachable(Str.c_str());
            }
        }
        else if(auto Load = dyn_cast<LoadInst>(UseLoc)){
            if(isAlias(LoadId, PtrId, Load)){
                for(auto Pe : getPointsToSet(PtrId, Loc)){
                    // outs() << "add use label " << Pe << "\n";                    
                    addUseLabel(Pe, Load);
                }
            }
        }
        else if(auto Ret = dyn_cast<ReturnInst>(UseLoc)){

            propagatePointsToInformation(Ret, Loc, PtrId);
            PointsToSetOut[Ret][PtrId] = PointsToSetIn[Ret][PtrId];


            //pass back to callsite
            for(auto CallSite : Func2CallerLocation[Ret->getFunction()]){

                auto CallPtrId = SteengaardResult.getID(CallSite, true);
                bool isChanged = false;
                for(auto Pointer : PointsToSetOut[Ret][PtrId]){
                    isChanged = isChanged || PointsToSetOut[CallSite][CallPtrId].insert(Pointer).second;
                }
                if(isChanged){
                    updateAliasUsers(CallSite, CallPtrId, PropagateList);
                    for(auto UseLoc : getAffectUseLocations(CallSite, CallPtrId)){    
                        PropagateList.insert(std::make_tuple(CallSite, UseLoc, CallPtrId));
                    }
                }

            }

        }
        else if(auto Call = dyn_cast<CallBase>(UseLoc)){
            
            if(!Call->getCalledFunction() || Call->getCalledFunction()->isDeclaration()){
                continue;
            }
            outs() << "1111111\n";
            auto PointerOpId = SteengaardResult.getID(Loc, true);
            outs() << "2222222\n";
            auto Pts = getPointsToSet(PointerOpId, Loc);
            size_t ArgIdx = 0;
            while(ArgIdx < Call->arg_size() && ArgIdx < Call->getCalledFunction()->arg_size()){
                if(Call->getArgOperand(ArgIdx) == Alias){
                    break;
                }
                ArgIdx++;
            }
            

            if(ArgIdx < Call->arg_size() && ArgIdx < Call->getCalledFunction()->arg_size()){
                outs() << ArgIdx << "\n";
                // update pts of parameter
                bool isUpdated = false;
                auto FirstInst = getFirstInst(Call->getCalledFunction());
                outs() << "3333333\n";
                auto ParameterId = SteengaardResult.getID(Call->getCalledFunction()->getArg(ArgIdx), true);
                outs() << "4444444\n";
                for(auto Pointee : Pts){
                    isUpdated = isUpdated || PointsToSetOut[FirstInst][ParameterId].insert(Pointee).second;
                }
                // todo: add function getAlias;
                AliasMap[FirstInst][ParameterId].insert(AliasMap[Loc][PointerOpId].begin(), AliasMap[Loc][PointerOpId].end());
                if(isUpdated){
                    updateAliasUsers(Call->getCalledFunction()->getArg(ArgIdx), ParameterId, PropagateList);
                }
            }


        }
        else if(auto BitCast = dyn_cast<BitCastInst>(UseLoc)){
            updateAliasUsers(BitCast, PtrId, PropagateList);
        }        
        else{
            DEBUG_WITH_TYPE("fspa", outs() << getCurrentTime() << " Cannot process alias user clause type: " 
                << *UseLoc << "\n");

        }
    

    }
}

/// @brief Update the PTS of \p ArgIdx-th parameter of Func.
void FlowSensitivePointerAnalysis::updateArgPointsToSetOfFunc(const Function *Func, std::set<size_t> PTS, 
    size_t ArgIdx, SetVector<DefUseEdgeTupleTy> &PropagateList){
    // Densemap has no at member function in llvm-14. Move back to use std::map.

    outs() << "UAPTSOF: " << Func->getName().str() << " " << ArgIdx << "\n";

    const Value *Parameter = Func->getArg(ArgIdx);
    
    auto FirstInst = getFirstInst(Func);
    auto ParameterId = SteengaardResult.getID(Parameter, true);
    auto OldSize = PointsToSetOut[FirstInst][ParameterId].size();
    PointsToSetOut[FirstInst][ParameterId].insert(PTS.begin(), PTS.end());

    if(OldSize != PointsToSetOut.at(FirstInst).at(ParameterId).size()){
        for(auto UseLoc : getAffectUseLocations(FirstInst, ParameterId)){    
            PropagateList.insert(std::make_tuple(FirstInst, UseLoc, ParameterId));
        }
        updateAliasUsers(Parameter, ParameterId, PropagateList);
    }
}

bool FlowSensitivePointerAnalysis::isAlias(size_t LoadId, size_t PtrId, const PointerTy *IRRELEVANT){
    if(LoadId == PtrId){
        return true;
    }

    const ProgramLocationTy *Loc = nullptr;
    auto QueriedPointer = SteengaardResult.getPtr(LoadId).first;
    if(auto Load = dyn_cast<LoadInst>(QueriedPointer)){
        Loc = Load;
    }
    else if(auto Arg = dyn_cast<Argument>(QueriedPointer)){
        Loc = getFirstInst(Arg->getParent());
    }


    // auto Inst = dyn_cast<Instruction>(Loc);
    return Loc && AliasMap[Loc][LoadId].count(PtrId);
}

/// @brief Propagate pointer information along def use graph until fix-point.
void FlowSensitivePointerAnalysis::propagate(SetVector<DefUseEdgeTupleTy> &PropagateList, 
    const Function *Func){
   
    while(!PropagateList.empty()){

        const auto [DefLoc, UseLoc, PtrId] = *(PropagateList.begin());
        PropagateList.erase(PropagateList.begin());
        
        DEBUG_WITH_TYPE("fspa", outs() << getCurrentTime() << " Propagating edge " << *DefLoc << " === " << PtrId << " ===> " << *UseLoc << "\n");

        propagatePointsToInformation(UseLoc, DefLoc, PtrId);

        

        if(auto Store = dyn_cast<StoreInst>(UseLoc)){
            if(isa<GlobalValue>(Store->getPointerOperand()->stripPointerCastsAndAliases()) || isa<GlobalValue>(Store->getValueOperand()->stripPointerCastsAndAliases()) || !Store->getValueOperand()->getType()->isPointerTy()){
                continue;
            }

            auto PointerOpId = SteengaardResult.getID(Store->getPointerOperand()->stripPointerCastsAndAliases(), true);
            auto ValueOpId = SteengaardResult.getID(Store->getValueOperand()->stripPointerCastsAndAliases(), true);
            updatePointsToSet(UseLoc, PtrId, getPointsToSet(ValueOpId, DefLoc), PropagateList);
            
            if(isAlias(PointerOpId, PtrId, Store)){
                for(auto Pe : PointsToSetOut[UseLoc][PtrId]){
                    addUseLabel(Pe, Store);
                }
            }
        }
        else if(auto Load = dyn_cast<LoadInst>(UseLoc)){

            if(!Load->getType()->isPointerTy()){
                continue;
            }

            bool PtsIsChanged = false;
            auto OldPts = PointsToSetOut[UseLoc][PtrId];
            PointsToSetOut[UseLoc][PtrId] = PointsToSetIn.at(UseLoc).at(PtrId);
            if(OldPts != PointsToSetOut[UseLoc][PtrId]){
                PtsIsChanged = true;
            }


            auto OldAliasSet = std::set<size_t>{};
            auto UseLocId = SteengaardResult.getID(UseLoc, true);
            
            if(AliasMap.count(UseLoc) && AliasMap[UseLoc].count(UseLocId)){
                OldAliasSet = AliasMap.at(UseLoc).at(UseLocId);
            }
            
            updateAliasInformation(UseLoc, UseLocId, SteengaardResult.getID(Load->getPointerOperand(), true));


            if(OldAliasSet != AliasMap.at(UseLoc).at(UseLocId) || PtsIsChanged){
                updateAliasUsers(UseLoc, PtrId, PropagateList);
            }
        }
        else if(auto Call = dyn_cast<CallBase>(UseLoc)){

            if(!Call->getCalledFunction() || Call->getCalledFunction()->isDeclaration()){
                // Ignore indirect call.
                continue;
            }

            if(!SteengaardResult.getPtr(PtrId).first->getType()->isPointerTy()){
                continue;
            }
            // Find corresponding parameter index from the actual argument.
            
            auto ArgumentIdxs = CallSite2ArgIdx[Call][PtrId];
            auto FirstInst = getFirstInst(Call->getCalledFunction());


            for(auto ArgumentIdx : ArgumentIdxs){

                assert(ArgumentIdx < Call->arg_size() && ArgumentIdx < Call->getCalledFunction()->arg_size() && "Arguemnt idx out of bound.");
                updateArgPointsToSetOfFunc(Call->getCalledFunction(), PointsToSetIn.at(UseLoc).at(PtrId), ArgumentIdx, PropagateList);
                // also update the alias information.
                AliasMap[FirstInst][SteengaardResult.getID(Call->getCalledFunction()->getArg(ArgumentIdx), true)].insert(PtrId);

            }
            //todo: if the in set in changed.
            bool isChanged = false;
            for(auto Pointer : PointsToSetIn[Call][PtrId]){
                isChanged = isChanged || PointsToSetOut[FirstInst][PtrId].insert(Pointer).second;
            }
            
            // PointsToSetOut[FirstInst][PtrId].insert(PointsToSetIn[Call][PtrId].begin(), PointsToSetIn[Call][PtrId].end());
            if(isChanged){
                for(auto Loc : getAffectUseLocations(FirstInst, PtrId)){
                    PropagateList.insert(std::make_tuple(FirstInst, Loc, PtrId));
                }
            }
            
        }
        else if(auto Return = dyn_cast<ReturnInst>(UseLoc)){
            PointsToSetOut[UseLoc][PtrId] = PointsToSetIn.at(UseLoc).at(PtrId);
            if(SteengaardResult.getPtr(PtrId).first->getType()->isPointerTy()){
                for(auto CallSite : Func2CallerLocation[Return->getFunction()]){

                    bool isChanged = false;
                    for(auto Pointer : PointsToSetOut[UseLoc][PtrId]){
                        isChanged = isChanged || PointsToSetOut[CallSite][PtrId].insert(Pointer).second;
                    }
                    if(isChanged){
                        for(auto UseLoc : getAffectUseLocations(CallSite, PtrId)){    
                            PropagateList.insert(std::make_tuple(CallSite, UseLoc, PtrId));
                        }
                    }
                }
            }
        }
        else if(auto Phi = dyn_cast<PHINode>(UseLoc)){
            bool PtsIsChanged = false;
            auto OldPts = PointsToSetOut[UseLoc][PtrId];
            PointsToSetOut[UseLoc][PtrId].insert(PointsToSetIn.at(UseLoc).at(PtrId).begin(), PointsToSetIn.at(UseLoc).at(PtrId).end());
            if(OldPts != PointsToSetOut[UseLoc][PtrId]){
                PtsIsChanged = true;
            }
            if(PtsIsChanged){
                updateAliasUsers(UseLoc, PtrId, PropagateList);
            }
        }

    }
    return;
}

/// @brief Build Dominator graph for \p PtrId in function \p Func
/// @return A pair - the first element is a map that maps each instruction in the dominator graph to its def locations.
///     The seconds element is the dominator graph.
std::pair<std::map<const Instruction*, std::set<const Instruction*>>, DomGraph> 
    FlowSensitivePointerAnalysis::buildDominatorGraph(const Function *Func, size_t PtrId){

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
                if(getPointsToSet(SteengaardResult.getID(dyn_cast<StoreInst>(Node)->getPointerOperand(), true), Node).size() <= 1){
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

/// @brief Get all pointers with pointer level \p PointerLevel in worklist of function \p Func/ 
const std::set<size_t>& FlowSensitivePointerAnalysis::getPointersInWorkList(size_t PointerLevel, const Function *Func){
    if(Func2WorkList.count(Func) && Func2WorkList[Func].count(PointerLevel)){
        return Func2WorkList.at(Func).at(PointerLevel);
    }
    static std::set<size_t> empty;
    return empty;
}

void FlowSensitivePointerAnalysis::verify(Module &M){
    auto SourceFileName = M.getSourceFileName();
    auto DotPosition = SourceFileName.rfind('.');
    auto ExpectedOutPutFileName = SourceFileName.substr(0, DotPosition) + ".lfspa.out";

    std::ifstream ifs(ExpectedOutPutFileName);
    if(!ifs){
        llvm_unreachable("Cannot open expected output file.");
    }
    std::string Line;
    std::pair<size_t, size_t> TotalPtsSizeAndPtsCount;
    while(std::getline(ifs, Line)){

        std::istringstream iss(Line);
        std::vector<size_t> Nums;
        std::string Num;

        while(std::getline(iss, Num, ',')){
            Nums.push_back(std::stoul(Num));
        }
        TotalPtsSizeAndPtsCount.first = Nums[0];
        TotalPtsSizeAndPtsCount.second = Nums[1];
    }

    size_t TotalPtsSize = 0, NumPts = 0;
    for(auto Pair : PointsToSetOut){
        for(auto P : Pair.second){
            if(!SteengaardResult.getPtr(P.first).second){
                TotalPtsSize += P.second.size();
                NumPts += 1;    
            }   
        }
    }


    auto CorrectAnswer = (TotalPtsSize == TotalPtsSizeAndPtsCount.first && NumPts == TotalPtsSizeAndPtsCount.second);
    if(!CorrectAnswer){
        outs() << "Incorrect points to set result\n";

        dumpWorkList();
        dumpLabelMap();
        dumpDefUseGraph();
        dumpPointsToSet();
        dumpPointsToSetIn();
        dumpAliasMap();

        llvm_unreachable("Incorrect pointer level.");
    }
    else{
        outs() << "LFSPA test passed.\n";
    }

    llvm_unreachable("End of LFSPA.");
}



/// @brief Main entry of flow sensitive pointer analysis. Process pointer variables level by level. 
FlowSensitivePointerAnalysisResult FlowSensitivePointerAnalysis::run(Module &m, ModuleAnalysisManager &mam){
    DEBUG_WITH_TYPE("fspa", outs() << getCurrentTime() << " Start analyzing module " << m.getName() << "\n");
    SteengaardResult = mam.getResult<SteengaardAnalysis>(m);


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

        for(auto &Func : m.functions()){
            auto Pointers = getPointersInWorkList(CurrentPointerLevel, &Func);
            // for(auto PtrId : Pointers){
            //     const auto& [Out, DG] = buildDominatorGraph(&Func, PtrId);
            //     buildDefUseGraph(getUseLocations(PtrId), PtrId, Out, DG);
            // }
            auto PropagateList = initializePropagateList(Pointers, CurrentPointerLevel, &Func);
            propagate(PropagateList, &Func);
        }
        --CurrentPointerLevel;
    }

    auto stop = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(stop - start);




    // dumpPointsToSet();
    // dumpWorkList();
    // dumpDefUseGraph();


    outs() << "Runtime: " << duration.count() << "ms\n";
    computeAvgPtsSize();
    

    DEBUG_WITH_TYPE("lfspa", verify(m));

    return FlowSensitivePointerAnalysisResult(PointsToSetOut);
}

AnalysisKey FlowSensitivePointerAnalysis::Key;
namespace llvm{
    bool operator<(const Label &L1, const Label &L2){
        if(L1.Type == L2.Type){
            return L1.Ptr < L2.Ptr;
        }
        else{
            return L1.Type < L2.Type;
        }
    }

    raw_ostream& operator<<(raw_ostream &OS, const Label &L){
        
        
        if(L.Type == Label::LabelType::None){
            OS << "None";
        }
        else if(L.Type == Label::LabelType::Def){
            OS << "Def(";
            if(L.Ptr){
                OS << L.Ptr << ")";
            }
            else{
                OS << "nullptr)";
            }
        }
        else if(L.Type == Label::LabelType::Use){
            OS << "Use(";
            if(L.Ptr){
                OS << L.Ptr << ")";
            }
            else{
                OS << "nullptr)";
            }
        }
        else if(L.Type == Label::LabelType::DefUse){
            OS << "DefUse(";
            if(L.Ptr){
                OS << L.Ptr << ")";
            }
            else{
                OS << "nullptr)";
            }
        }
        return OS;
    }

}


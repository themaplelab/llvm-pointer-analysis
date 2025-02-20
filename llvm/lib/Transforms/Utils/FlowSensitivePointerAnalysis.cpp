#include "llvm/Transforms/Utils/FlowSensitivePointerAnalysis.h"


#include "llvm/Support/Debug.h"
#include "llvm/Support/Timer.h"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <sstream>


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
        DEBUG_WITH_TYPE("pts", dbgs() << "At program location" << *Loc << ":\n");
        for(auto PtsForPtr : PointsToSetOut.at(Loc)){
            auto Ptr = SteengaardResult.getPtr(PtsForPtr.first);
            std::string PtrType = Ptr.second ? "(TopLevel)" : "(AddrTaken)";
            if(dyn_cast<Argument>(Ptr.first)){
                DEBUG_WITH_TYPE("pts", dbgs() << "\t" << *(Ptr.first) << " " << PtrType << " ==>\n");
            }
            else{
                DEBUG_WITH_TYPE("pts", dbgs() << *(Ptr.first) << " " << PtrType << " ==>\n");
            }
            
            for(auto PointeeId : PtsForPtr.second){
                auto Pointee = SteengaardResult.getPtr(PointeeId);
                std::string PtrType = Pointee.second ? "(TopLevel)" : "(AddrTaken)";
                if(!Pointee.first){
                    DEBUG_WITH_TYPE("pts", dbgs() << "\t " << "nullptr" << "\n");
                }
                else if(dyn_cast<Instruction>(Pointee.first)){
                    DEBUG_WITH_TYPE("pts", dbgs() << "\t" << *(Pointee.first) << " " << PtrType << "\n");
                }
                else{
                    DEBUG_WITH_TYPE("pts", dbgs() << "\t " << *(Pointee.first) << " " << PtrType << "\n");
                }
            }
        }
    }

}

void FlowSensitivePointerAnalysis::dumpPointsToSet(){
    dbgs() << "Print points-to set stats\n";
    // C++26 will treat _ as a special value that does not cause unused warning.
    for(auto PtsForPtr : PointsToSetOut){
        printPointsToSetAtProgramLocation(PtsForPtr.first);
    }
}

void FlowSensitivePointerAnalysis::dumpAliasMap(){
    DEBUG_WITH_TYPE("pts", dbgs() << "Print alias map stats\n");
    // C++26 will treat _ as a special value that does not cause unused warning.
    for(auto LocAndPtr : AliasMap){
           if(PointsToSetOut.count(LocAndPtr.first)){
            DEBUG_WITH_TYPE("pts", dbgs() << "At program location" << *LocAndPtr.first << ":\n");
            for(auto AliasForPtr : AliasMap.at(LocAndPtr.first)){
                auto Ptr = SteengaardResult.getPtr(AliasForPtr.first).first;
                DEBUG_WITH_TYPE("pts", dbgs() << *Ptr << " alias to \n");
                for(auto PointeeId : AliasForPtr.second){
                    auto Pointee = SteengaardResult.getPtr(PointeeId).first;

                    if(!Pointee){
                        DEBUG_WITH_TYPE("pts", dbgs() << "\t " << "nullptr" << "\n");
                    }
                    else if(dyn_cast<Instruction>(Pointee)){
                        DEBUG_WITH_TYPE("pts", dbgs() << "\t" << *Pointee << "\n");
                    }
                    else{
                        DEBUG_WITH_TYPE("alias", dbgs() << "\t " << *Pointee << "\n");
                    }
                }
            }
        }

    }
}

void FlowSensitivePointerAnalysis::dumpLabelMap(){

    dbgs() << "Print label map\n";
    for(auto p : LabelMap){
        dbgs() << "Labels at" << *p.first << "\n";
        for(auto e : p.second){
            dbgs() << "\t" << e << "\n";
        }
    }

}

/// @brief Initialize analysis for all functions in current module. 
/// @return The largest pointer level among all functions.
void FlowSensitivePointerAnalysis::globalInitialize(Module &M, SteengaardAnalysisResult &SAR){

    // for(auto &Global : M.globals()){
    //     if(Global.getType()->isPointerTy()){
    //         auto PointerLevel = computePointerLevel(&Global);
    //         GlobalWorkList[PointerLevel].insert(&Global);
    //     }
    // }

    for(auto &Func : M.functions()){
        initialize(&Func, SAR);
    }

    AnalysisResult.setWorkList(Func2WorkList);
}

/// @brief Compute the pointer level of an allocated pointer.
/// @return Pointer level for \p Ptr.
size_t FlowSensitivePointerAnalysis::computePointerLevel(const PointerTy *Ptr, bool isTopLevel, SteengaardAnalysisResult &SAR){

    const auto &PointerLevel = SAR.getPointerLevels();
    auto Id = SAR.getID(Ptr, isTopLevel);
    if(!PointerLevel.count(Id)){
        errs() << *Ptr << " " << isTopLevel << "\n";
        llvm_unreachable("Cannot get pointer level for a missing pointer.");
    }
    return PointerLevel.at(Id);
}

void FlowSensitivePointerAnalysis::addDefLabel(size_t Ptr, const ProgramLocationTy *Loc, const Function *Func){
    LabelMap[Loc].insert(Label(Ptr, Label::LabelType::Def));
    DefLocations[Ptr][Func].insert(Loc);

    return;
}

void FlowSensitivePointerAnalysis::addUseLabel(size_t Ptr, const ProgramLocationTy *Loc){
    LabelMap[Loc].insert(Label(Ptr, Label::LabelType::Use));
    UseList[Ptr].insert(Loc);

    return;
}

/// @brief Calculate pointer level for function \p Func. Mark labels for each pointer
///     related instructions. Store pointers into worklist according to their pointer level.
void FlowSensitivePointerAnalysis::initialize(const Function *Func, SteengaardAnalysisResult &SAR){

    /*
        1. get result of steengaard analysis.
        2. for leaf node in pts graph, pl = 0
        3. pl(x) = max(pl(y), y is child of x) + 1
    */

    DEBUG_WITH_TYPE("fspa", dbgs() << getCurrentTime() << " Initializing function "
         << Func->getName() << "\n");

    WorkListTy WorkList;

    // function parameters
    if(!Func->isDeclaration()){
        auto FirstInst = Func->getEntryBlock().getFirstNonPHIOrDbg();
        for(auto &Arg : Func->args()){
            if(!Arg.getType()->isPointerTy()){
                continue;
            }
            auto ArgId = SAR.getID(&Arg, true);
            addDefLabel(ArgId, FirstInst, Func);
            PointsToSetOut[FirstInst][ArgId] = std::set<size_t>{};
            auto PointerLevel = computePointerLevel(&Arg, true, SAR);
            WorkList[PointerLevel].insert(ArgId);
        }
    }

    
    for(auto &Inst : instructions(*Func)){
        // x = alloca ptr
        if(const AllocaInst *Alloca = dyn_cast<AllocaInst>(&Inst)){

            auto AllocaMemoryObjPl = computePointerLevel(Alloca, false, SAR);
            auto AllocaAddrTakenId = SAR.getID(Alloca, false);
            WorkList[AllocaMemoryObjPl].insert(AllocaAddrTakenId);
            addDefLabel(AllocaAddrTakenId, Alloca, Func);
            // todo: add id for nullptr
            PointsToSetOut[&Inst][AllocaAddrTakenId] = std::set<size_t>{};
            PointsToSetIn[&Inst][AllocaAddrTakenId] = std::set<size_t>{};




            auto AllocaTopLevelId = SAR.getID(Alloca, true);
            auto PointerLevel = computePointerLevel(Alloca, true, SAR);
            WorkList[PointerLevel].insert(AllocaTopLevelId);
            addDefLabel(AllocaTopLevelId, Alloca, Func);
            // A -> nullptr means A is not initialized. It helps us to find dereference of nullptr.
            PointsToSetOut[&Inst][AllocaTopLevelId] = std::set<size_t>{AllocaAddrTakenId};
            PointsToSetIn[&Inst][AllocaTopLevelId] = std::set<size_t>{AllocaAddrTakenId};


        }
        // callgraph
        else if(const CallInst *Call = dyn_cast<CallInst>(&Inst)){
            Func2CallerLocation[Call->getCalledFunction()].insert(Call);
            if(!Call->getCalledFunction()){
                DEBUG_WITH_TYPE("warning", dbgs() << getCurrentTime() << " WARNING:" 
                    << *Call << " performs an indirect call\n");
            }
            else{
                Caller2Callee[Func].insert(Call->getCalledFunction());
                size_t idx = 0;
                for(auto &Arg : Call->args()){
                    // outs() << idx << " " << *Arg << " \n";
                    auto ArgId = SteengaardResult.getID(Arg, true);
                    if(Arg && Arg->getType()->isPointerTy()){
                        CallSite2ArgIdx[Call][ArgId].insert(idx);
                    }
                    ++idx;
                }
            }          
        }
        else if(const ReturnInst *Return = dyn_cast<ReturnInst>(&Inst)){
            Func2TerminateBBs[Func].insert(Return->getParent());
            for(auto &Arg : Func->args()){
                addUseLabel(SAR.getID(&Arg, true), Return);
            }
        }
    }

    Func2WorkList.emplace(Func, WorkList);
}


/// @brief Check if a program location defines a pointer \p Ptr.
bool FlowSensitivePointerAnalysis::hasDef(const ProgramLocationTy *Loc, size_t PtrId){
    // outs() << "HasDef At" << *loc << " with ptr" << *ptr << "\n";
    if(!LabelMap.count(Loc)){
        return false;
    }
    auto iter = std::find_if(LabelMap.at(Loc).begin(), LabelMap.at(Loc).end(), [&](Label L) -> bool {
        return L.Type == Label::LabelType::Def && L.Ptr == PtrId;
        });
    return (iter == LabelMap.at(Loc).end() ? false : true);
}

std::set<size_t> FlowSensitivePointerAnalysis::getPointsToSet(size_t PtrId, const ProgramLocationTy *Loc){
    auto PtrIsTopLevel = SteengaardResult.getPtr(PtrId).second;
    auto Ptr = SteengaardResult.getPtr(PtrId).first;

    // outs() << "GPTS: " << PtrId << " at " << *Loc << "\n";

    if(PtrIsTopLevel){
        // pts of top-level variable only defined once.
        if(auto Alloca = dyn_cast<AllocaInst>(Ptr)){
            return PointsToSetOut.at(Alloca).at(PtrId);
        }
        else if(auto Load = dyn_cast<LoadInst>(Ptr)){
            // get all alias of Ptr
            // merge all pts of alias
            std::set<size_t> res;

            // dumpAliasMap();

            // todo: make it use at, not operator[]
            for(auto AliasId : AliasMap[Loc][PtrId]){
                // outs() << "1231231231231323\n";
                res.insert(PointsToSetIn.at(Loc).at(AliasId).begin(), PointsToSetIn.at(Loc).at(AliasId).end());
            }
            return res;
        }  
        else if(auto Arg = dyn_cast<Argument>(Ptr)){
            auto FirstInst = Loc->getFunction()->getEntryBlock().getFirstNonPHIOrDbg();
            auto ArgId = SteengaardResult.getID(Arg, true);
            return PointsToSetOut[FirstInst][ArgId];
        }
        else{
            llvm_unreachable("toplevel ptr has unknown type");
        }
    }
    else{
        llvm_unreachable("cannot get pts of addrtaken variable");
    }
}


/// @brief Mark def and use labels for pointer \p Ptr. The labels are later 
/// used for building def use graph.
void FlowSensitivePointerAnalysis::markLabelsForPtr(const PointerTy *Ptr, bool isTopLevel){

    if(!isTopLevel){
        errs() << "Marking labels for addr-taken " << *Ptr << " " << isTopLevel << "\n";
        return; 
    }

    DEBUG_WITH_TYPE("pts", dbgs() << getCurrentTime() << " Marking labels for " << *Ptr << "\n");

    auto PtrId = SteengaardResult.getID(Ptr, true);
    for(auto User : Ptr->users()){
        
        if(auto *Store = dyn_cast<StoreInst>(User)){
            // Ptr->users() has different meaning than the def and use in our
            // analysis. We do not want to mark label for X if we have store X Y.
            if(Store->getValueOperand() == Ptr){
                continue;
            }
            // get points-to set of Ptr at Store.
            auto Pts = getPointsToSet(PtrId, Store);
            for(auto PointeeId : Pts){
                addDefLabel(PointeeId, Store, Store->getFunction());
                addUseLabel(PointeeId, Store);
            }


            // addDefLabel(SteengaardResult.getID(Ptr, true), Store, Store->getFunction());
            // addUseLabel(SteengaardResult.getID(Ptr, true), Store);
        }
        else if(auto *Load = dyn_cast<LoadInst>(User)){
            auto Pts = getPointsToSet(PtrId, Load);
            for(auto PointeeId : Pts){
                addUseLabel(PointeeId, Load);
            }
            for(auto UseOfIntermediate : Load->users()){
                if(auto UseLoc = dyn_cast<Instruction>(UseOfIntermediate)){
                    for(auto PointeeId : Pts){
                        addUseLabel(PointeeId, UseLoc);
                    }
                    if(auto Call = dyn_cast<CallBase>(UseLoc)){
                        // outs() << "12313213323123\n";
                        for(auto PointeeId : Pts){
                            // find argidx of matching argument.
                            auto ArgIdx = 0;
                            while(ArgIdx < Call->arg_size()){
                                if(Call->getArgOperand(ArgIdx) == Load){
                                    break;
                                }
                                ArgIdx++;
                            }
                            // outs() << "ArgIdx: " << ArgIdx << " " << *Load << " " << *Call->getArgOperand(ArgIdx) << "\n";
                            if(ArgIdx < Call->arg_size()){
                                CallSite2ArgIdx[Call][PointeeId] = CallSite2ArgIdx[Call][SteengaardResult.getID(Load, true)];
                            }
                            
                        }
                    }  
                }
            }
        }
        else if(auto *Call = dyn_cast<CallInst>(User)){
            // todo: no new marks for call. new labels should only be introduced when the callee define/use a pointer created in other function.
            // addDefLabel(SteengaardResult.getID(Ptr, true), Call, Call->getFunction());
            // addUseLabel(SteengaardResult.getID(Ptr, true), Call);
        }
        else if(auto *Return = dyn_cast<ReturnInst>(User)){
            if(Return->getReturnValue()->getType()->isPointerTy()){
                addUseLabel(SteengaardResult.getID(Ptr, true), Return);
            }
            
        }
        else if(dyn_cast<GetElementPtrInst>(User) || dyn_cast<BitCastInst>(User) || 
                dyn_cast<CmpInst>(User) || dyn_cast<InvokeInst>(User) || dyn_cast<VAArgInst>(User) || 
                dyn_cast<PHINode>(User) || dyn_cast<PtrToIntInst>(User)){

            DEBUG_WITH_TYPE("warning", dbgs() << getCurrentTime() << "WARNING:" << *User << " is in the user list of pointer "
                << *Ptr << ", but it's neither storeinst nor loadinst.\n");
        }
        else{
            std::string Str;
            raw_string_ostream(Str) << *User;
            Str = "Cannot process instruction:" + Str + "\n";
            llvm_unreachable(Str.c_str());
        }
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

    DEBUG_WITH_TYPE("dfg", dbgs() << getCurrentTime() << " Add def Use edge " 
        << *Def << " === " << PtrId << " ===> " << *Use << "\n");
    DefUseGraph[Def][PtrId].insert(Use);
}

/// @brief Create and insert def use edge for pointer \p Ptr.
void FlowSensitivePointerAnalysis::buildDefUseGraph(std::set<const ProgramLocationTy*> UseLocs, 
    size_t PtrId, std::map<const Instruction*, std::set<const Instruction*>> OUT, DomGraph DG){
    for(auto UseLoc : UseLocs){
        DEBUG_WITH_TYPE("fspa", dbgs() << getCurrentTime() << " Building def-use graph for " 
            << PtrId << " at " << *UseLoc << "\n");

        // find all def in dg that dominates useLoc
        auto Nodes = DG.getNodes();
        std::set<const ProgramLocationTy *> Dom{};
        for(auto Node : Nodes){
            // dbgs() << "NODE " << *Node << "\n";
            if(Func2DomTree.at(UseLoc->getFunction()).get().dominates(Node, UseLoc)){
                Dom.insert(Node);
                // dbgs() << "INSERT " << *Node << "\n";
            }
        }

        // find immediate dominator
        if(Dom.empty()){
            return;
        }
        auto IDom = *(Dom.begin());
        for(auto D : Dom){
            if(D == IDom){
                continue;
            }

            if(Func2DomTree.at(UseLoc->getFunction()).get().dominates(IDom, D)){
                IDom = D;
            }

        }

        // out[idom] are the defs
        DEBUG_WITH_TYPE("fspa", dbgs() << getCurrentTime() << " Found immediate dominator " << *IDom
            << " for " << *UseLoc << "\n");

        auto DefLocs = OUT[IDom];
        auto it0 = Nodes.find(UseLoc);
        auto it1 = DefLocations[PtrId][UseLoc->getFunction()].find(UseLoc);
        if(it0 != Nodes.end() && it1 == DefLocations[PtrId][UseLoc->getFunction()].end()){
            DefLocs = OUT[UseLoc];
        }

        DEBUG_WITH_TYPE("fspa", dbgs() << getCurrentTime() << " Found " << DefLocs.size() 
            << " def locations of pointer" << PtrId << " at " << *UseLoc << "\n");

        for(auto Def : DefLocs){
            addDefUseEdge(Def, UseLoc, PtrId);
        }
    }
}

/// @brief Build def use graph for all global variables of pointer level \p PtrLvl
void FlowSensitivePointerAnalysis::processGlobalVariables(size_t PtrLvl){

    // if(GlobalWorkList.count(PtrLvl)){
    //     for(auto GlobalPtr : GlobalWorkList.at(PtrLvl)){
    //         markLabelsForPtr(GlobalPtr);
    //         auto UseLocs = getUseLocations(GlobalPtr);
    //         buildDefUseGraph(UseLocs, GlobalPtr);
    //     }
    // }
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

    DEBUG_WITH_TYPE("pts", dbgs() << getCurrentTime() << " Got " << Res.size() 
        << " affect use locations for " << PtrId << " at " << *Loc << "\n");        
    return Res;
}

/// @brief Find all def use edges starting from the allocation location of pointers.
///     When we start propagating points-to information, we want to start with these edges.
SetVector<FlowSensitivePointerAnalysis::DefUseEdgeTupleTy> FlowSensitivePointerAnalysis::
    initializePropagateList(std::set<size_t> Pointers, size_t PtrLvl, const Function *Func, SteengaardAnalysisResult &SAR){

    SetVector<DefUseEdgeTupleTy> PropagateList{};
    for(auto PtrId: Pointers){
        auto Ptr = SAR.getPtr(PtrId).first;
        if(!Func->isDeclaration()){
            if(auto Arg = dyn_cast<Argument>(Ptr)){
                auto ArgId = SteengaardResult.getID(Arg, true);
                auto FirstInst = Func->getEntryBlock().getFirstNonPHIOrDbg();
                auto InitialDUEdges = getAffectUseLocations(FirstInst, ArgId);
                for(auto UseLoc : InitialDUEdges){
                    PropagateList.insert(std::make_tuple(FirstInst, UseLoc, ArgId));
                }
                continue;
            }
        }
        
        // An allocated pointer in llvm also represents its allocation location.
        auto Loc = dyn_cast<ProgramLocationTy>(Ptr);
        assert(Loc && "Cannot use nullptr as program location");
        auto InitialDUEdges = getAffectUseLocations(Loc, PtrId);
        for(auto UseLoc : InitialDUEdges){
            PropagateList.insert(std::make_tuple(Loc, UseLoc, PtrId));
        }
    }
    // if(GlobalWorkList.count(PtrLvl)){
    //     for(auto Ptr : GlobalWorkList.at(PtrLvl)){
    //         // Global variables are not supported yet.
    //     }
    // }
    return PropagateList;
}

/// @brief Pass PTS(Ptr) from DefLoc to UseLoc, skip store instruction.
void FlowSensitivePointerAnalysis::propagatePointsToInformation(const ProgramLocationTy *UseLoc,
     const ProgramLocationTy *DefLoc, size_t PtrId){
        PointsToSetIn[UseLoc][PtrId];
        for(auto PointerId : PointsToSetOut.at(DefLoc).at(PtrId)){
            auto Pointer = SteengaardResult.getPtr(PointerId).first;
            if(!isa_and_nonnull<LoadInst>(Pointer)){
                PointsToSetIn[UseLoc][PtrId].insert(PointerId);
            }
        }

    return;
}

/// @brief Get the set of real pointee represented by a pointer. For a storeInst 
///     store x y, x maybe an parameter of a function or a temporary register. In our
///     analysis, we only want to propagate allocated pointer. Return itself iff no
///     allocated pointers are alias to it.
/// @param Loc Program location that we want to query the alias set.
/// @param ValueOperand Value operand of a store instruction. We will find all allocated pointers that
///     alias to it.
/// @return  A set of allocated pointers or \p ValueOperand.
std::set<size_t> FlowSensitivePointerAnalysis::
    getRealPointsToSet(const ProgramLocationTy *Loc, const PointerTy *ValueOperand){
        // todo: the logic is incorrect. for store x y, it should be pts(z) = pts(x) for all z pointed by y.
    
    std::set<size_t> Pointees{};

    auto ValueOperandId = SteengaardResult.getID(ValueOperand, true);

    Pointees.insert(SteengaardResult.getID(ValueOperand->stripPointerCasts(), true));
    if(AliasMap.count(Loc) && AliasMap[Loc].count(ValueOperandId)){
        Pointees = AliasMap.at(Loc).at(ValueOperandId);
    }
    
    return Pointees;
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
     size_t PointerId, std::set<size_t> AdjustedPointsToSet, 
     SetVector<DefUseEdgeTupleTy> &PropagateList){

    // assert(Pointer && "Cannot update PTS for nullptr");
    auto Store = dyn_cast<StoreInst>(Loc);

    auto AliasSet = std::set<const PointerTy *>{};

    if(auto Load = dyn_cast<LoadInst>(Store->getPointerOperand())){
        if(PointsToSetOut.count(Load) && PointsToSetOut[Load].count(SteengaardResult.getID(Load->getPointerOperand(), true))){
            for(auto PtrId : PointsToSetOut[Load][SteengaardResult.getID(Load->getPointerOperand(), true)]){
                auto Ptr = SteengaardResult.getPtr(PtrId).first;
                if(Ptr && !isa<LoadInst>(Ptr)){
                    AliasSet.insert(Ptr);
                }
            }
        }
    }
    
    if(AliasSet.size() <= 1){
        // Strong update
        if(updatePointsToSetAtProgramLocation(Loc, PointerId, AdjustedPointsToSet)){
            for(auto UseLoc : getAffectUseLocations(Loc, PointerId)){    
                PropagateList.insert(std::make_tuple(Loc, UseLoc, PointerId));
            }
        }
    }
    else{
        // Weak update
        for(auto Alias : AliasSet){
            if(dyn_cast<LoadInst>(Alias)){
                continue;
            }
            auto AliasId = SteengaardResult.getID(Alias, true);
            PointsToSetOut[Loc][AliasId] = PointsToSetIn[Loc][AliasId];
            if(insertPointsToSetAtProgramLocation(Loc, AliasId, AdjustedPointsToSet)){
                for(auto UseLoc : getAffectUseLocations(Loc, AliasId)){    
                    PropagateList.insert(std::make_tuple(Loc, UseLoc, AliasId));
                }
            }
        }
    }

    
}

/// @brief Get all alias for the pointer operand of a load instruction.
/// If no such alias, return the pointer itself.
std::set<size_t> FlowSensitivePointerAnalysis::
    getAlias(const ProgramLocationTy *Loc, const LoadInst *Load){

    auto PointerOpId = SteengaardResult.getID(Load->getPointerOperand(), true);

    if(AliasMap.count(Loc) && AliasMap[Loc].count(PointerOpId)){
        return AliasMap.at(Loc).at(PointerOpId);
    }
    else{
        return std::set<size_t>{PointerOpId};
    }
}

/// @brief Update the alias set of pointer x introduced by a \p loadInst 'x = load y'.
void FlowSensitivePointerAnalysis::updateAliasInformation(const ProgramLocationTy *Loc, const LoadInst *Load){
    
    auto Aliases = getAlias(Loc, Load);
    // outs() << "UAI: " << Aliases.size() << "\n";
    for(auto &AliasId : Aliases){
        if(!AliasId){
            continue;
        }
        assert(AliasId && "Cannot process nullptr");


        // if we have x = load y, and y alias z, we will need pts(z) at current program location.
        auto Alias = SteengaardResult.getPtr(AliasId).first;
        if(!AliasUser.count(Alias)){
            for(auto User : Alias->users()){
                AliasUser[Alias].insert(User);
            }
        }
        AliasUser[Alias].insert(Loc);
        if(PointsToSetOut.count(Loc) && PointsToSetOut[Loc].count(AliasId)){
            // bug: should be assign not insert
            AliasMap[Loc][SteengaardResult.getID(Load, true)].insert(PointsToSetOut.at(Loc).at(AliasId).begin(), PointsToSetOut.at(Loc).at(AliasId).end());
        }
    }

    // outs() << "new alias map\n";
    // for(auto p : AliasMap[Loc]){
    //     outs() << p.first << " alias \n";
    //     for(auto e : p.second){
    //         outs() << e << ", ";
    //     }
    //     outs() << "\n";
    // }
    return;
}

/// @brief Find all pointers that points to \p Ptr at \p Loc.
std::vector<size_t> FlowSensitivePointerAnalysis::
    ptsPointsTo(const ProgramLocationTy *Loc, const PointerTy *Ptr){

    std::vector<size_t> Res{};

    if(PointsToSetOut.count(Loc)){
        for(auto PtsAtPtr : PointsToSetOut.at(Loc)){
            if(AliasMap[Loc][SteengaardResult.getID(dyn_cast<StoreInst>(Loc)->getPointerOperand(), true)].count(PtsAtPtr.first) || 
                PtsAtPtr.first == SteengaardResult.getID(dyn_cast<StoreInst>(Loc)->getPointerOperand(), true)){
                    Res.push_back(PtsAtPtr.first);
                }

        }
    }
    
    return Res;
}

/// @brief Propagate the alias set of \p Loc at \p Loc to its use locations.
///     Update its user accordingly.
void FlowSensitivePointerAnalysis::updateAliasUsers(const ProgramLocationTy *Loc, 
    SetVector<DefUseEdgeTupleTy> &PropagateList){

    
    // dbgs() << AliasUser.count(Loc) << "\n";
    for(auto User : AliasUser.at(Loc)){      

        DEBUG_WITH_TYPE("pts", dbgs() << getCurrentTime() << " Updating alias user for pointer " 
            << *Loc << " at " << *User << "\n");  
        
        auto UseLoc = dyn_cast<ProgramLocationTy>(User);
        auto Ptr = dyn_cast<PointerTy>(Loc);
        auto PtrId = SteengaardResult.getID(Ptr, true);

        if(AliasMap.count(Loc) && AliasMap[Loc].count(PtrId)){
            AliasMap[UseLoc][PtrId] = AliasMap.at(Loc).at(PtrId);
        }else{
            AliasMap[UseLoc][PtrId] = std::set<size_t>{};
        }
        
        if(auto Store = dyn_cast<StoreInst>(UseLoc)){
            if(Ptr == Store->getPointerOperand()){ 
                // if user is 'store x y', and we are passing alias-set(y), we need to make
                // pts(z) = pts(y) for each z in alias-set(y)
                auto PtrId = SteengaardResult.getID(Ptr, true);
                for(auto AliasId : AliasMap.at(UseLoc).at(PtrId)){
                    if(!AliasId){
                        DEBUG_WITH_TYPE("warning", dbgs() << getCurrentTime() << " WARNING: try to update pts for nullptr at"
                            << *UseLoc << " because nullptr is alias to " << *Ptr << "\n");
                            continue;
                    }
                    addDefLabel(AliasId, UseLoc, UseLoc->getFunction());
                    addUseLabel(AliasId, UseLoc);
                }
                
            }
            else if(Ptr == Store->getValueOperand()){
                
                auto Pointers = ptsPointsTo(UseLoc, Ptr);
                for(auto PointerId : Pointers){

                    auto OldPTS = std::set<size_t>{};
                    if(PointsToSetOut.count(UseLoc) && PointsToSetOut[UseLoc].count(PointerId)){
                        OldPTS = PointsToSetOut.at(UseLoc).at(PointerId);
                    }
                    auto PtrId = SteengaardResult.getID(Ptr, true);
                    PointsToSetOut[UseLoc][PointerId].insert(
                        AliasMap.at(UseLoc).at(PtrId).begin(), 
                        AliasMap.at(UseLoc).at(PtrId).end());


                    if(OldPTS != PointsToSetOut.at(UseLoc).at(PointerId)){
                        auto Pointer = SteengaardResult.getPtr(PointerId).first;
                        for(auto AffectedLoc : getAffectUseLocations(UseLoc, PointerId)){    
                                PropagateList.insert(std::make_tuple(UseLoc, AffectedLoc, PointerId));
                        }
                    }
                }
            }
            else{
                std::string Str;
                raw_string_ostream(Str) << "Hitting at " << *Store << " with pointer " << *Loc << "\n";
                llvm_unreachable(Str.c_str());
            }
        }
        else if(auto Load = dyn_cast<LoadInst>(UseLoc)){
            for(auto AliasId : AliasMap.at(Load).at(PtrId)){
                addUseLabel(AliasId, Load);
            }
        }
        else if(auto Ret = dyn_cast<ReturnInst>(UseLoc)){
            for(auto AliasId : AliasMap.at(UseLoc).at(PtrId)){
                auto Alias = SteengaardResult.getPtr(AliasId).first;
                if(Alias && (dyn_cast<AllocaInst>(Alias) || dyn_cast<LoadInst>(Alias))){
                    addUseLabel(AliasId, UseLoc);
                }
            }
        }
        else if (auto Call = dyn_cast<CallInst>(UseLoc)){
            if(!Call->getCalledFunction() || Call->getCalledFunction()->isDeclaration()){
                // Ignore indirect call.
                continue;
            }
            // Find corresponding parameter index from the actual argument.
            size_t ArgumentIdx = 0;
            for(auto &Arg : Call->args()){
                if(Arg == Ptr){
                    break;
                }
                ++ArgumentIdx;
            }

            assert((ArgumentIdx < Call->arg_size()) && "Cannot find argument index at function.");
            for(auto AliasId : AliasMap.at(UseLoc).at(PtrId)){
                auto Alias = SteengaardResult.getPtr(AliasId).first;

                // addDefLabel(AliasId, UseLoc, UseLoc->getFunction());
                // addUseLabel(AliasId, UseLoc);
                CallSite2ArgIdx[Call][AliasId].insert(ArgumentIdx);
            }
        }
        else{
            DEBUG_WITH_TYPE("fspa", dbgs() << getCurrentTime() << " Cannot process alias user clause type: " 
                << *UseLoc << "\n");

        }
    }
}

/// @brief Update the PTS of \p ArgIdx-th parameter of Func.
void FlowSensitivePointerAnalysis::updateArgPointsToSetOfFunc(const Function *Func, std::set<size_t> PTS, 
    size_t ArgIdx, SetVector<DefUseEdgeTupleTy> &PropagateList){
    // Densemap has no at member function in llvm-14. Move back to use std::map.

    // outs() << "UAPTSOF: " << Func->getName().str() << " " << ArgIdx << "\n";

    const Value *Parameter;
    for(auto &Para : Func->args()){
        Parameter = &Para;
        if(!ArgIdx){
            break;
        }
        --ArgIdx;
    }

    
    auto FirstInst = Func->getEntryBlock().getFirstNonPHIOrDbg();
    auto ParameterId = SteengaardResult.getID(Parameter, true);
    auto OldSize = PointsToSetOut[FirstInst][ParameterId].size();
    PointsToSetOut[FirstInst][ParameterId].insert(PTS.begin(), PTS.end());

    if(OldSize != PointsToSetOut.at(FirstInst).at(ParameterId).size()){
        for(auto UseLoc : getAffectUseLocations(FirstInst, ParameterId)){    
            PropagateList.insert(std::make_tuple(FirstInst, UseLoc, ParameterId));
        }
    }
}

/// @brief Propagate pointer information along def use graph until fix-point.
void FlowSensitivePointerAnalysis::propagate(SetVector<DefUseEdgeTupleTy> PropagateList, 
    const Function *Func){
   
    while(!PropagateList.empty()){

        auto Edge = PropagateList.front();
        auto DefLoc = std::get<0>(Edge);
        auto UseLoc = std::get<1>(Edge);
        auto PtrId = std::get<2>(Edge);

        assert(DefLoc && "Cannot have nullptr as def loc");
        assert(UseLoc && "Cannot have nullptr as use loc");
        // assert(PtrId && "Cannot have nullptr as pointer");


        DEBUG_WITH_TYPE("pts", dbgs() << getCurrentTime() << " Propagating edge " 
            << *DefLoc << " === " << PtrId << " ===> " << *UseLoc << "\n");

        propagatePointsToInformation(UseLoc, DefLoc, PtrId);

        if(auto Store = dyn_cast<StoreInst>(UseLoc)){
            auto ValueOpId = SteengaardResult.getID(Store->getValueOperand(), true);
            auto PTS = getPointsToSet(ValueOpId, Store);
            // outs() << "Pts:\n";
            // for(auto pe : PTS){
            //     outs() << pe <<  ", ";
            // }
            // outs() << "\n";
            updatePointsToSet(UseLoc, PtrId, PTS, PropagateList);
        }
        else if(auto Load = dyn_cast<LoadInst>(UseLoc)){
            PointsToSetOut[UseLoc][PtrId] = PointsToSetIn.at(UseLoc).at(PtrId);
            auto OldAliasSet = std::set<size_t>{};
            auto UseLocId = SteengaardResult.getID(UseLoc, true);
            if(AliasMap.count(UseLoc) && AliasMap[UseLoc].count(UseLocId)){
                OldAliasSet = AliasMap.at(UseLoc).at(UseLocId);
            }

            updateAliasInformation(UseLoc, Load);
            
            auto NewAliasSet = std::set<size_t>{};
            if(AliasMap.count(UseLoc) && AliasMap[UseLoc].count(UseLocId)){
                NewAliasSet = AliasMap.at(UseLoc).at(UseLocId);
            }

            if(OldAliasSet != NewAliasSet){
                if(!AliasUser.count(UseLoc)){
                    // Create empty entry
                    AliasUser[UseLoc];
                    for(auto user : UseLoc->users()){
                        AliasUser[UseLoc].insert(user);
                    }
                }
                updateAliasUsers(UseLoc, PropagateList);
            }
        }
        else if(auto Call = dyn_cast<CallInst>(UseLoc)){
            if(!Call->getCalledFunction() || Call->getCalledFunction()->isDeclaration()){
                // Ignore indirect call.
                PropagateList.erase(PropagateList.begin());
                continue;
            }

            if(!SteengaardResult.getPtr(PtrId).first->getType()->isPointerTy()){
                continue;
            }
            // Find corresponding parameter index from the actual argument.
            auto ArgumentIdxs = CallSite2ArgIdx[Call][PtrId];
            // outs() << "Callsite2ArgIdx\n";
            // for(auto p : CallSite2ArgIdx){
            //     outs() << *p.first << "\n";
            //     for(auto e : p.second){
            //         outs() << e.first << " => \n";
            //         for(auto ee : e.second){
            //             outs() << ee << ",";
            //         }
            //         outs() << "\n";
            //     }
            // }
            // outs() << PtrId << " ArgumentIdxs: " << ArgumentIdxs.size() << "\n";
            for(auto ArgumentIdx : ArgumentIdxs){
                assert(ArgumentIdx < Call->arg_size() && "Arguemnt idx out of bound.");
                updateArgPointsToSetOfFunc(Call->getCalledFunction(), PointsToSetIn.at(UseLoc).at(PtrId), ArgumentIdx, PropagateList);
            }

        }
        else if(auto Return = dyn_cast<ReturnInst>(UseLoc)){

            // todo : remove idx finding 
            PointsToSetOut[UseLoc][PtrId] = PointsToSetIn.at(UseLoc).at(PtrId);
            // only propagate ptr parameter.
            if(dyn_cast<Argument>(SteengaardResult.getPtr(PtrId).first) && SteengaardResult.getPtr(PtrId).first->getType()->isPointerTy()){
                size_t ParaIdx = 0;
                for(auto &Arg : Return->getFunction()->args()){
                    if(&Arg == SteengaardResult.getPtr(PtrId).first){
                        break;
                    }
                    ++ParaIdx;
                }

                for(auto CallSite : Func2CallerLocation[Return->getFunction()]){
                    const Value *Arg = CallSite->getOperand(ParaIdx);
                    auto ArgId = SteengaardResult.getID(Arg, true);

                    auto Changed = insertPointsToSetAtProgramLocation(CallSite, ArgId, PointsToSetOut[Return][PtrId]);
                    if(Changed){
                        for(auto UseLoc : getAffectUseLocations(CallSite, ArgId)){    
                            PropagateList.insert(std::make_tuple(CallSite, UseLoc, ArgId));
                        }
                    }
                    for(auto AliasId : AliasMap[CallSite][ArgId]){
                        // auto Alias = SteengaardResult.getPtr(AliasId).first;
                        if(insertPointsToSetAtProgramLocation(CallSite, AliasId, PointsToSetOut[Return][PtrId])){
                        for(auto UseLoc : getAffectUseLocations(CallSite, AliasId)){    
                            PropagateList.insert(std::make_tuple(CallSite, UseLoc, AliasId));
                        }
                    }
                    }
                }
            }
        }

        PropagateList.erase(PropagateList.begin());
    }
    return;
}


std::pair<std::map<const Instruction*, std::set<const Instruction*>>, DomGraph> 
    FlowSensitivePointerAnalysis::buildDominatorGraph(const Function *Func, size_t PtrId){


    DomGraph DG;

    // add all instruction labeled with def(ptr) in function func to dg
    for(auto Node : DefLocations[PtrId][Func]){
        DG.addNode(Node);
    }

    // get idf

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


    // fix point solution in dg

    std::map<const Instruction*, std::set<const Instruction*>> IN;
    std::map<const Instruction*, std::set<const Instruction*>> OUT;


    while(true){
        bool Changed = false;
        auto Nodes = DG.getNodes();
        auto Edges = DG.getEdges();

        for(auto Node : DG.getNodes()){
            // update out
            auto OldOut = OUT[Node];
            auto AliasSet = std::set<size_t>{};
            if(AliasMap.count(Node) && AliasMap[Node].count(PtrId)){
                AliasSet = AliasMap.at(Node).at(PtrId);
            }
            if(DefLocations[PtrId][Func].find(Node) == DefLocations[PtrId][Func].end()){
                OUT[Node] = IN[Node];
            }
            else if(AliasSet.size() <= 1){
                OUT[Node] = std::set<const ProgramLocationTy*>{Node};
            }
            else{
                OUT[Node] = IN[Node];
                OUT[Node].insert(Node);
            }

            if(OldOut != OUT[Node]){
                Changed = true;
            }

            // update in
            for(auto ToNode : Edges[Node]){
                auto OldIN = IN[ToNode];
                IN[ToNode].insert(OUT[Node].begin(), OUT[Node].end());
                if(OldIN != IN[ToNode]){
                    Changed = true;
                }
            }
        }

        if(!Changed){
            break;
        }
    }

    return {OUT, DG};

}



/// @brief Main entry of flow sensitive pointer analysis. Process pointer
///        variables level by level. 
/// @param m 
/// @param mam 
/// @return A FlowSensitivePointerAnalysisResult that records points-to 
///         set for variables at each program location
FlowSensitivePointerAnalysisResult FlowSensitivePointerAnalysis::run(Module &m, ModuleAnalysisManager &mam){


    DEBUG_WITH_TYPE("fspa", dbgs() << getCurrentTime() << " Start analyzing module " 
        << m.getName() << "\n");

    auto start = std::chrono::high_resolution_clock::now();

    SteengaardResult = mam.getResult<SteengaardAnalysis>(m);
    

    auto CurrentPointerLevel = SteengaardResult.getMaxPl();
    globalInitialize(m, SteengaardResult);

    // outs() << "Worklist\n";
    // for(auto p : Func2WorkList){
    //     outs() << "Function " << p.first->getName().str() << "\n";
    //     for(auto wl : p.second){
    //         outs() << "Pointer level: " << wl.first << "\n";
    //         for(auto e : wl.second){
    //             outs() << "\t" << e << "\n";
    //         }
    //     }
        
    // }

    auto &FAM = mam.getResult<FunctionAnalysisManagerModuleProxy>(m).getManager();
    
    
    while(CurrentPointerLevel > 1){
        for(auto &Func : m.functions()){
            if(Func.isDeclaration()){
                continue;
            }

            Func2DomTree.emplace(&Func, FAM.getResult<DominatorTreeAnalysis>(Func));
            Func2DomFrontier.emplace(&Func, FAM.getResult<DominanceFrontierAnalysis>(Func));

            auto Pointers = std::set<size_t>{};
            if(Func2WorkList.count(&Func) && Func2WorkList[&Func].count(CurrentPointerLevel)){
                Pointers = Func2WorkList.at(&Func).at(CurrentPointerLevel);
            }
            for(auto PtrId : Pointers){
                auto Ptr = SteengaardResult.getPtr(PtrId);
                markLabelsForPtr(Ptr.first, Ptr.second);
            }
        }

        dumpLabelMap();
        

        for(auto &Func : m.functions()){
            // dbgs() << getCurrentTime() << " Analyzing function: " 
            //     << Func.getName() << " with pointer level: " << CurrentPointerLevel << "\n";

            auto Pointers = std::set<size_t>{};
            if(Func2WorkList.count(&Func) && Func2WorkList[&Func].count(CurrentPointerLevel)){
                Pointers = Func2WorkList.at(&Func).at(CurrentPointerLevel);
            }
            for(auto PtrId : Pointers){
                auto Pair = buildDominatorGraph(&Func, PtrId);
                auto OUT = Pair.first;
                auto DG = Pair.second;
                auto UseLocs = getUseLocations(PtrId);
                buildDefUseGraph(UseLocs, PtrId, OUT, DG);
            }

            auto PropagateList = initializePropagateList(Pointers, CurrentPointerLevel, &Func, SteengaardResult);
            // outs() << "Propagate list size for function " << Func.getName().str() << " is: " <<  PropagateList.size() << "\n";

            // dbgs() << getCurrentTime() << " Propagating function: " 
            //     << Func.getName() << " with pointer level: " << CurrentPointerLevel << "\n";
            // Also save caller when passing the arguments.
            propagate(PropagateList, &Func);

        }
        dumpPointsToSet();
        --CurrentPointerLevel;
        if(CurrentPointerLevel == 1){
            llvm_unreachable("test");
        }
        

    }

    auto stop = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(stop - start);
 
    // To get the value of duration use the count()
    // member function on the duration object
    dbgs() << duration.count() << "\n";


    // DEBUG_WITH_TYPE("label", dumpLabelMap());
    DEBUG_WITH_TYPE("pts", dumpPointsToSet());

    // DEBUG_WITH_TYPE("pts", dumpPointsToSet());
    // dumpAliasMap();

    AnalysisResult.setFunc2Pointers(Func2AllocatedPointersAndParameterAliases);
    AnalysisResult.setPointsToSet(PointsToSetOut);

    // size_t TotalPtsSize = 0, NumPts = 0;
    // for(auto Pair : PointsToSetOut){
    //     for(auto P : Pair.second){
    //         TotalPtsSize += P.second.size();
    //         NumPts += 1;
    //     }
    // }

    // dbgs() << "End of analysis. Avg Pts Size is " << (double)TotalPtsSize / NumPts << "\n";

    return AnalysisResult;
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


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
            if(dyn_cast<Argument>(PtsForPtr.first)){
                DEBUG_WITH_TYPE("pts", dbgs() << "\t" << *(PtsForPtr.first) << " ==>\n");
            }
            else{
                DEBUG_WITH_TYPE("pts", dbgs() << *(PtsForPtr.first) << " ==>\n");
            }
            
            for(auto Pointee : PtsForPtr.second){
                if(!Pointee){
                    DEBUG_WITH_TYPE("pts", dbgs() << "\t " << "nullptr" << "\n");
                }
                else if(dyn_cast<Instruction>(Pointee)){
                    DEBUG_WITH_TYPE("pts", dbgs() << "\t" << *Pointee << "\n");
                }
                else{
                    DEBUG_WITH_TYPE("pts", dbgs() << "\t " << *Pointee << "\n");
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
    DEBUG_WITH_TYPE("alias", dbgs() << "Print alias map stats\n");
    // C++26 will treat _ as a special value that does not cause unused warning.
    for(auto LocAndPtr : AliasMap){
           if(PointsToSetOut.count(LocAndPtr.first)){
            DEBUG_WITH_TYPE("alias", dbgs() << "At program location" << *LocAndPtr.first << ":\n");
            for(auto AliasForPtr : AliasMap.at(LocAndPtr.first)){
                DEBUG_WITH_TYPE("alias", dbgs() << *(AliasForPtr.first) << " alias to \n");
                for(auto Pointee : AliasForPtr.second){
                    if(!Pointee){
                        DEBUG_WITH_TYPE("alias", dbgs() << "\t " << "nullptr" << "\n");
                    }
                    else if(dyn_cast<Instruction>(Pointee)){
                        DEBUG_WITH_TYPE("alias", dbgs() << "\t" << *Pointee << "\n");
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
size_t FlowSensitivePointerAnalysis::globalInitialize(Module &M){

    // for(auto &Global : M.globals()){
    //     if(Global.getType()->isPointerTy()){
    //         auto PointerLevel = computePointerLevel(&Global);
    //         GlobalWorkList[PointerLevel].insert(&Global);
    //     }
    // }

    size_t PtrLvl = 0;
    for(auto &Func : M.functions()){
        auto PL = initialize(&Func);
        if(PtrLvl < PL){
            PtrLvl = PL;
        }
    }

    assert(PtrLvl >= 0 && "Pointer level cannot be negative.");

    AnalysisResult.setWorkList(Func2WorkList);
    return PtrLvl;
}

/// @brief Compute the pointer level of an allocated pointer.
/// @return Pointer level for \p Ptr.
size_t FlowSensitivePointerAnalysis::computePointerLevel(const PointerTy *Ptr){

    // dbgs() << "Compute ptr level for " << *Ptr << "\n";

    size_t PointerLevel = 1;

    auto ty = Ptr->getType();
    while(ty->getPointerElementType()->isPointerTy()){
        ++PointerLevel;
        ty = ty->getPointerElementType();
    }
    return PointerLevel;
}

void FlowSensitivePointerAnalysis::addDefLabel(const PointerTy *Ptr, const ProgramLocationTy *Loc, const Function *Func){
    LabelMap[Loc].insert(Label(Ptr, Label::LabelType::Def));
    DefLocations[Ptr][Func].insert(Loc);

    return;
}

void FlowSensitivePointerAnalysis::addUseLabel(const PointerTy *Ptr, const ProgramLocationTy *Loc){
    LabelMap[Loc].insert(Label(Ptr, Label::LabelType::Use));
    UseList[Ptr].insert(Loc);

    return;
}

/// @brief Calculate pointer level for function \p Func. Mark labels for each pointer
///     related instructions. Store pointers into worklist according to their pointer level.
size_t FlowSensitivePointerAnalysis::initialize(const Function *Func){

    DEBUG_WITH_TYPE("fspa", dbgs() << getCurrentTime() << " Initializing function "
         << Func->getName() << "\n");

    WorkListTy WorkList;
    size_t res = 0;

    if(!Func->isDeclaration()){
        auto FirstInst = Func->getEntryBlock().getFirstNonPHIOrDbg();
        for(auto &Arg : Func->args()){
            if(!Arg.getType()->isPointerTy()){
                continue;
            }
            addDefLabel(&Arg, FirstInst, Func);
            PointsToSetOut[FirstInst][&Arg] = std::set<const Value*>{};
            auto PointerLevel = computePointerLevel(&Arg);
            WorkList[PointerLevel].insert(&Arg);
        }
    }

    for(auto &Inst : instructions(*Func)){
        if(const AllocaInst *Alloca = dyn_cast<AllocaInst>(&Inst)){
            auto PointerLevel = computePointerLevel(Alloca);
            WorkList[PointerLevel].insert(Alloca);
            if(PointerLevel > res){
                res = PointerLevel;
            }
            addDefLabel(Alloca, Alloca, Func);
            // A -> nullptr means A is not initialized. It helps us to find dereference of nullptr.
            PointsToSetOut[&Inst][Alloca] = std::set<const Value*>{nullptr};
        }
        else if(const CallInst *Call = dyn_cast<CallInst>(&Inst)){
            Func2CallerLocation[Call->getCalledFunction()].insert(Call);
            if(!Call->getCalledFunction()){
                // dbgs() << *Call << "\n";
                // if(isa<Function>(Call->getCalledOperand()->stripPointerCasts())){
                //     dbgs() << Call->getCalledOperand()->stripPointerCasts()->getName() << "\n";
                // }
                // else{
                //     dbgs() << *Call->getCalledOperand()->stripPointerCasts() << "\n";
                // }
                DEBUG_WITH_TYPE("warning", dbgs() << getCurrentTime() << " WARNING:" 
                    << *Call << " performs an indirect call\n");
            }
            else{
                Caller2Callee[Func].insert(Call->getCalledFunction());
                size_t idx = 0;
                for(auto Arg : Call->operand_values()){
                    if(!Arg && !isa<LoadInst>(Arg) && Arg->getType()->isPointerTy()){
                        CallSite2ArgIdx[Call][Arg].insert(idx);
                    }
                    ++idx;
                }
            }          
        }
        else if(const ReturnInst *Return = dyn_cast<ReturnInst>(&Inst)){
            Func2TerminateBBs[Func].insert(Return->getParent());
            for(auto &Arg : Func->args()){
                addUseLabel(&Arg, Return);
            }
            
        }
    }

    Func2WorkList.emplace(Func, WorkList);
    return res;
}


/// @brief Check if a program location defines a pointer \p Ptr.
bool FlowSensitivePointerAnalysis::hasDef(const ProgramLocationTy *Loc, const PointerTy *Ptr){
    // outs() << "HasDef At" << *loc << " with ptr" << *ptr << "\n";
    if(!LabelMap.count(Loc)){
        return false;
    }
    auto iter = std::find_if(LabelMap.at(Loc).begin(), LabelMap.at(Loc).end(), [&](Label L) -> bool {
        return L.Type == Label::LabelType::Def && L.Ptr == Ptr;
        });
    return (iter == LabelMap.at(Loc).end() ? false : true);
}

/// @brief Mark def and use labels for pointer \p Ptr. The labels are later 
/// used for building def use graph.
void FlowSensitivePointerAnalysis::markLabelsForPtr(const PointerTy *Ptr){

    DEBUG_WITH_TYPE("fspa", dbgs() << getCurrentTime() << " Marking labels for "
         << *Ptr << "\n");

    
    for(auto User : Ptr->users()){
        
        if(auto *Store = dyn_cast<StoreInst>(User)){
            // Ptr->users() has different meaning than the def and use in our
            // analysis. We do not want to mark label for X if we have store X Y.
            if(Store->getValueOperand() == Ptr){
                continue;
            }
            addDefLabel(Ptr, Store, Store->getFunction());
            addUseLabel(Ptr, Store);
        }
        else if(auto *Load = dyn_cast<LoadInst>(User)){
            addUseLabel(Ptr, Load);
        }
        else if(auto *Call = dyn_cast<CallInst>(User)){
            addDefLabel(Ptr, Call, Call->getFunction());
            addUseLabel(Ptr, Call);
        }
        else if(auto *Return = dyn_cast<ReturnInst>(User)){
            addUseLabel(Ptr, Return);
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
std::set<const FlowSensitivePointerAnalysis::ProgramLocationTy*> FlowSensitivePointerAnalysis::getUseLocations(const PointerTy *Ptr){
    if(UseList.count(Ptr)){
        return UseList.at(Ptr);
    }
    return std::set<const ProgramLocationTy*>{};
}

/// @brief Add def use graph for pointer \p Ptr.
void FlowSensitivePointerAnalysis::addDefUseEdge(const ProgramLocationTy *Def, const ProgramLocationTy *Use, const PointerTy *Ptr){

    DEBUG_WITH_TYPE("dfg", dbgs() << getCurrentTime() << " Add def Use edge " 
        << *Def << " === " << *Ptr << " ===> " << *Use << "\n");
    DefUseGraph[Def][Ptr].insert(Use);
}

/// @brief Create and insert def use edge for pointer \p Ptr.
void FlowSensitivePointerAnalysis::buildDefUseGraph(std::set<const ProgramLocationTy*> UseLocs, 
    const PointerTy *Ptr, std::map<const Instruction*, std::set<const Instruction*>> OUT, DomGraph DG){
    for(auto UseLoc : UseLocs){
        DEBUG_WITH_TYPE("fspa", dbgs() << getCurrentTime() << " Building def-use graph for " 
            << *Ptr << " at " << *UseLoc << "\n");

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
        auto it1 = DefLocations[Ptr][UseLoc->getFunction()].find(UseLoc);
        if(it0 != Nodes.end() && it1 == DefLocations[Ptr][UseLoc->getFunction()].end()){
            DefLocs = OUT[UseLoc];
        }

        DEBUG_WITH_TYPE("fspa", dbgs() << getCurrentTime() << " Found " << DefLocs.size() 
            << " def locations of pointer" << *Ptr << " at " << *UseLoc << "\n");

        for(auto Def : DefLocs){
            addDefUseEdge(Def, UseLoc, Ptr);
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
    getAffectUseLocations(const ProgramLocationTy *Loc, const PointerTy *Ptr){

    std::vector<const ProgramLocationTy*> Res{};
    if(DefUseGraph.count(Loc)){
        for(auto UseLocsAndPtr : DefUseGraph.at(Loc)){
            if(Ptr == UseLocsAndPtr.first){
                Res.insert(Res.begin(), UseLocsAndPtr.second.begin(), UseLocsAndPtr.second.end());
            }
        }
    }

    DEBUG_WITH_TYPE("fspa", dbgs() << getCurrentTime() << " Got " << Res.size() 
        << " affect use locations for " << *Ptr << " at " << *Loc << "\n");        
    return Res;
}

/// @brief Find all def use edges starting from the allocation location of pointers.
///     When we start propagating points-to information, we want to start with these edges.
SetVector<FlowSensitivePointerAnalysis::DefUseEdgeTupleTy> FlowSensitivePointerAnalysis::
    initializePropagateList(std::set<const PointerTy*> Pointers, size_t PtrLvl, const Function *Func){

    SetVector<DefUseEdgeTupleTy> PropagateList{};
    for(auto Ptr: Pointers){
        if(!Func->isDeclaration()){
            if(auto Arg = dyn_cast<Argument>(Ptr)){
                auto FirstInst = Func->getEntryBlock().getFirstNonPHIOrDbg();
                auto InitialDUEdges = getAffectUseLocations(FirstInst, Arg);
                for(auto UseLoc : InitialDUEdges){
                    PropagateList.insert(std::make_tuple(FirstInst, UseLoc, Arg));
                }
                continue;
            }
        }
        
        // An allocated pointer in llvm also represents its allocation location.
        auto Loc = dyn_cast<ProgramLocationTy>(Ptr);
        assert(Loc && "Cannot use nullptr as program location");
        auto InitialDUEdges = getAffectUseLocations(Loc, Ptr);
        for(auto UseLoc : InitialDUEdges){
            PropagateList.insert(std::make_tuple(Loc, UseLoc, Ptr));
        }
    }
    if(GlobalWorkList.count(PtrLvl)){
        for(auto Ptr : GlobalWorkList.at(PtrLvl)){
            // Global variables are not supported yet.
        }
    }
    return PropagateList;
}

/// @brief Pass PTS(Ptr) from DefLoc to UseLoc, skip store instruction.
void FlowSensitivePointerAnalysis::propagatePointsToInformation(const ProgramLocationTy *UseLoc,
     const ProgramLocationTy *DefLoc, const PointerTy *Ptr){
        PointsToSetIn[UseLoc][Ptr];
        for(auto P : PointsToSetOut.at(DefLoc).at(Ptr)){
            if(!isa_and_nonnull<LoadInst>(P)){
                PointsToSetIn[UseLoc][Ptr].insert(P);
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
std::set<const FlowSensitivePointerAnalysis::PointerTy*> FlowSensitivePointerAnalysis::
    getRealPointsToSet(const ProgramLocationTy *Loc, const PointerTy *ValueOperand){
    
    std::set<const PointerTy*> Pointees{};

    Pointees.insert(ValueOperand->stripPointerCasts());
    if(AliasMap.count(Loc) && AliasMap[Loc].count(ValueOperand)){
        Pointees = AliasMap.at(Loc).at(ValueOperand);
    }
    
    return Pointees;
}

/// @brief Update points-to-set for \p Ptr at program location \p Loc.
/// @return True if the points-to set is changed.
bool FlowSensitivePointerAnalysis::updatePointsToSetAtProgramLocation(const ProgramLocationTy *Loc, 
    const PointerTy *Ptr, std::set<const PointerTy*> &PTS){

    auto OldPTS = std::set<const Value*>{};
    if(PointsToSetOut.count(Loc) && PointsToSetOut[Loc].count(Ptr)){
        OldPTS = PointsToSetOut.at(Loc).at(Ptr);
    }
    
    if(OldPTS != PTS){
        PointsToSetOut[Loc][Ptr] = PTS;
        return true;
    }
    return false;
}

bool FlowSensitivePointerAnalysis::insertPointsToSetAtProgramLocation(const ProgramLocationTy *Loc, 
    const PointerTy *Ptr, std::set<const PointerTy*> &PTS){
        bool Changed = false;
        for(auto Pointer : PTS){
            if(PointsToSetOut[Loc][Ptr].insert(Pointer).second){
                Changed = true;
            }
        }

        return Changed;
}

/// @brief Perform either strong update or weak update for \p Pointer at \p Loc
///     according to the size of aliases of \p Pointer. Add def-use edges to
///     \p PropagateList if needed.
void FlowSensitivePointerAnalysis::updatePointsToSet(const ProgramLocationTy *Loc,
     const PointerTy *Pointer, std::set<const PointerTy *> AdjustedPointsToSet, 
     SetVector<DefUseEdgeTupleTy> &PropagateList){

    assert(Pointer && "Cannot update PTS for nullptr");
    auto Store = dyn_cast<StoreInst>(Loc);

    auto AliasSet = std::set<const PointerTy *>{};

    if(auto Load = dyn_cast<LoadInst>(Store->getPointerOperand())){
        if(PointsToSetOut.count(Load) && PointsToSetOut[Load].count(Load->getPointerOperand())){
            for(auto Ptr : PointsToSetOut[Load][Load->getPointerOperand()]){
                if(Ptr && !isa<LoadInst>(Ptr)){
                    AliasSet.insert(Ptr);
                }
            }
        }
    }
    
    if(AliasSet.size() <= 1){
        // Strong update
        if(updatePointsToSetAtProgramLocation(Loc, Pointer, AdjustedPointsToSet)){
            for(auto UseLoc : getAffectUseLocations(Loc, Pointer)){    
                PropagateList.insert(std::make_tuple(Loc, UseLoc, Pointer));
            }
        }
    }
    else{
        // Weak update
        for(auto Alias : AliasSet){
            if(dyn_cast<LoadInst>(Alias)){
                continue;
            }
            PointsToSetOut[Loc][Alias] = PointsToSetIn[Loc][Alias];
            if(insertPointsToSetAtProgramLocation(Loc, Alias, AdjustedPointsToSet)){
                for(auto UseLoc : getAffectUseLocations(Loc, Alias)){    
                    PropagateList.insert(std::make_tuple(Loc, UseLoc, Alias));
                }
            }
        }
    }
}

/// @brief Get all alias for the pointer operand of a load instruction.
/// If no such alias, return the pointer itself.
std::set<const FlowSensitivePointerAnalysis::PointerTy*> FlowSensitivePointerAnalysis::
    getAlias(const ProgramLocationTy *Loc, const LoadInst *Load){

    if(AliasMap.count(Loc) && AliasMap[Loc].count(Load->getPointerOperand())){
        return AliasMap.at(Loc).at(Load->getPointerOperand());
    }
    else{
        return std::set<const PointerTy*>{Load->getPointerOperand()};
    }
}

/// @brief Update the alias set of pointer x introduced by a \p loadInst 'x = load y'.
void FlowSensitivePointerAnalysis::updateAliasInformation(const ProgramLocationTy *Loc, const LoadInst *Load){
    
    auto Aliases = getAlias(Loc, Load);
    for(auto &Alias : Aliases){
        if(!Alias){
            continue;
        }
        assert(Alias && "Cannot process nullptr");

        // if we have x = load y, and y alias z, we will need pts(z) at current program location.
        if(!AliasUser.count(Alias)){
            for(auto User : Alias->users()){
                AliasUser[Alias].insert(User);
            }
        }
        AliasUser[Alias].insert(Loc);
        if(PointsToSetOut.count(Loc) && PointsToSetOut[Loc].count(Alias)){
            // bug: should be assign not insert
            AliasMap[Loc][Load].insert(PointsToSetOut.at(Loc).at(Alias).begin(), PointsToSetOut.at(Loc).at(Alias).end());
        }
    }
    return;
}

/// @brief Find all pointers that points to \p Ptr at \p Loc.
std::vector<const FlowSensitivePointerAnalysis::PointerTy*> FlowSensitivePointerAnalysis::
    ptsPointsTo(const ProgramLocationTy *Loc, const PointerTy *Ptr){

    std::vector<const PointerTy*> Res{};

    if(PointsToSetOut.count(Loc)){
        for(auto PtsAtPtr : PointsToSetOut.at(Loc)){
            if(AliasMap[Loc][dyn_cast<StoreInst>(Loc)->getPointerOperand()].count(PtsAtPtr.first) || 
                PtsAtPtr.first == dyn_cast<StoreInst>(Loc)->getPointerOperand()){
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

        DEBUG_WITH_TYPE("fspa", dbgs() << getCurrentTime() << " Updating alias user for pointer " 
            << *Loc << " at " << *User << "\n");  
        
        auto UseLoc = dyn_cast<ProgramLocationTy>(User);
        auto Ptr = dyn_cast<PointerTy>(Loc);

        if(AliasMap.count(Loc) && AliasMap[Loc].count(Ptr)){
            AliasMap[UseLoc][Ptr] = AliasMap.at(Loc).at(Ptr);
        }else{
            AliasMap[UseLoc][Ptr] = std::set<const PointerTy*>{};
        }
        
        if(auto Store = dyn_cast<StoreInst>(UseLoc)){
            if(Ptr == Store->getPointerOperand()){ 
                // if user is 'store x y', and we are passing alias-set(y), we need to make
                // pts(z) = pts(y) for each z in alias-set(y)
                for(auto Alias : AliasMap.at(UseLoc).at(Ptr)){
                    if(!Alias){
                        DEBUG_WITH_TYPE("warning", dbgs() << getCurrentTime() << " WARNING: try to update pts for nullptr at"
                            << *UseLoc << " because nullptr is alias to " << *Ptr << "\n");
                            continue;
                    }
                    addDefLabel(Alias, UseLoc, UseLoc->getFunction());
                    addUseLabel(Alias, UseLoc);
                }
                
            }
            else if(Ptr == Store->getValueOperand()){
                
                auto Pointers = ptsPointsTo(UseLoc, Ptr);
                for(auto Pointer : Pointers){

                    auto OldPTS = std::set<const PointerTy*>{};
                    if(PointsToSetOut.count(UseLoc) && PointsToSetOut[UseLoc].count(Pointer)){
                        OldPTS = PointsToSetOut.at(UseLoc).at(Pointer);
                    }
                                        
                    PointsToSetOut[UseLoc][Pointer].insert(
                        AliasMap.at(UseLoc).at(Ptr).begin(), 
                        AliasMap.at(UseLoc).at(Ptr).end());


                    if(OldPTS != PointsToSetOut.at(UseLoc).at(Pointer)){
                        for(auto AffectedLoc : getAffectUseLocations(UseLoc, Pointer)){    
                                PropagateList.insert(std::make_tuple(UseLoc, AffectedLoc, Pointer));
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
            for(auto Alias : AliasMap.at(Load).at(Ptr)){
                addUseLabel(Alias, Load);
            }
        }
        else if(auto Ret = dyn_cast<ReturnInst>(UseLoc)){
            for(auto Alias : AliasMap.at(UseLoc).at(Ptr)){
                if(Alias && (dyn_cast<AllocaInst>(Alias) || dyn_cast<LoadInst>(Alias))){
                    addUseLabel(Alias, UseLoc);
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
            for(auto Arg : Call->operand_values()){
                if(Arg == Ptr){
                    break;
                }
                ++ArgumentIdx;
            }

            assert((ArgumentIdx < Call->arg_size()) && "Cannot find argument index at function.");
            for(auto Alias : AliasMap.at(UseLoc).at(Ptr)){
                addDefLabel(Alias, UseLoc, UseLoc->getFunction());
                addUseLabel(Alias, UseLoc);
                CallSite2ArgIdx[Call][Alias].insert(ArgumentIdx);
            }
        }
        else{
            DEBUG_WITH_TYPE("fspa", dbgs() << getCurrentTime() << " Cannot process alias user clause type: " 
                << *UseLoc << "\n");

        }
    }
}

/// @brief Update the PTS of \p ArgIdx-th parameter of Func.
void FlowSensitivePointerAnalysis::updateArgPointsToSetOfFunc(const Function *Func, std::set<const Value*> PTS, 
    size_t ArgIdx, SetVector<DefUseEdgeTupleTy> &PropagateList){
    // Densemap has no at member function in llvm-14. Move back to use std::map.

    const Value *Parameter;
    for(auto &Para : Func->args()){
        Parameter = &Para;
        if(!ArgIdx){
            break;
        }
        --ArgIdx;
    }

    
    auto FirstInst = Func->getEntryBlock().getFirstNonPHIOrDbg();

    auto OldSize = PointsToSetOut[FirstInst][Parameter].size();
    PointsToSetOut[FirstInst][Parameter].insert(PTS.begin(), PTS.end());

    if(OldSize != PointsToSetOut.at(FirstInst).at(Parameter).size()){
        for(auto UseLoc : getAffectUseLocations(FirstInst, Parameter)){    
            PropagateList.insert(std::make_tuple(FirstInst, UseLoc, Parameter));
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
        auto Ptr = std::get<2>(Edge);

        assert(DefLoc && "Cannot have nullptr as def loc");
        assert(UseLoc && "Cannot have nullptr as use loc");
        assert(Ptr && "Cannot have nullptr as pointer");


        DEBUG_WITH_TYPE("fspa", dbgs() << getCurrentTime() << " Propagating edge " 
            << *DefLoc << " === " << *Ptr << " ===> " << *UseLoc << "\n");

        propagatePointsToInformation(UseLoc, DefLoc, Ptr);

        if(auto Store = dyn_cast<StoreInst>(UseLoc)){
            auto PTS = getRealPointsToSet(UseLoc, Store->getValueOperand());
            updatePointsToSet(UseLoc, Ptr, PTS, PropagateList);
        }
        else if(auto Load = dyn_cast<LoadInst>(UseLoc)){
            PointsToSetOut[UseLoc][Ptr] = PointsToSetIn.at(UseLoc).at(Ptr);
            auto OldAliasSet = std::set<const PointerTy*>{};
            if(AliasMap.count(UseLoc) && AliasMap[UseLoc].count(UseLoc)){
                OldAliasSet = AliasMap.at(UseLoc).at(UseLoc);
            }

            updateAliasInformation(UseLoc, Load);
            
            auto NewAliasSet = std::set<const PointerTy*>{};
            if(AliasMap.count(UseLoc) && AliasMap[UseLoc].count(UseLoc)){
                NewAliasSet = AliasMap.at(UseLoc).at(UseLoc);
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

            if(!Ptr->getType()->isPointerTy()){
                continue;
            }
            // Find corresponding parameter index from the actual argument.
            auto ArgumentIdxs = CallSite2ArgIdx[Call][Ptr];
            for(auto ArgumentIdx : ArgumentIdxs){
                assert(ArgumentIdx < Call->arg_size() && "Arguemnt idx out of bound.");
                updateArgPointsToSetOfFunc(Call->getCalledFunction(), PointsToSetIn.at(UseLoc).at(Ptr), ArgumentIdx, PropagateList);
            }

        }
        else if(auto Return = dyn_cast<ReturnInst>(UseLoc)){

            // todo : remove idx finding 
            PointsToSetOut[UseLoc][Ptr] = PointsToSetIn.at(UseLoc).at(Ptr);
            // only propagate ptr parameter.
            if(dyn_cast<Argument>(Ptr) && Ptr->getType()->isPointerTy()){
                size_t ParaIdx = 0;
                for(auto &Arg : Return->getFunction()->args()){
                    if(&Arg == Ptr){
                        break;
                    }
                    ++ParaIdx;
                }

                for(auto CallSite : Func2CallerLocation[Return->getFunction()]){
                    const Value *Arg = CallSite->getOperand(ParaIdx);

                    auto Changed = insertPointsToSetAtProgramLocation(CallSite, Arg, PointsToSetOut[Return][Ptr]);
                    if(Changed){
                        for(auto UseLoc : getAffectUseLocations(CallSite, Arg)){    
                            PropagateList.insert(std::make_tuple(CallSite, UseLoc, Arg));
                        }
                    }
                    for(auto Alias : AliasMap[CallSite][Arg]){
                        if(insertPointsToSetAtProgramLocation(CallSite, Alias, PointsToSetOut[Return][Ptr])){
                        for(auto UseLoc : getAffectUseLocations(CallSite, Alias)){    
                            PropagateList.insert(std::make_tuple(CallSite, UseLoc, Alias));
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
    FlowSensitivePointerAnalysis::buildDominatorGraph(const Function *Func, const PointerTy *Ptr){


    DomGraph DG;

    // add all instruction labeled with def(ptr) in function func to dg
    for(auto Node : DefLocations[Ptr][Func]){
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
            auto AliasSet = std::set<const PointerTy *>{};
            if(AliasMap.count(Node) && AliasMap[Node].count(Ptr)){
                AliasSet = AliasMap.at(Node).at(Ptr);
            }
            if(DefLocations[Ptr][Func].find(Node) == DefLocations[Ptr][Func].end()){
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


    auto CurrentPointerLevel = globalInitialize(m);

    auto &FAM = mam.getResult<FunctionAnalysisManagerModuleProxy>(m).getManager();
    
    
    while(CurrentPointerLevel){
        for(auto &Func : m.functions()){
            if(Func.isDeclaration()){
                continue;
            }

            Func2DomTree.emplace(&Func, FAM.getResult<DominatorTreeAnalysis>(Func));
            Func2DomFrontier.emplace(&Func, FAM.getResult<DominanceFrontierAnalysis>(Func));

            auto Pointers = std::set<const PointerTy*>{};
            if(Func2WorkList.count(&Func) && Func2WorkList[&Func].count(CurrentPointerLevel)){
                Pointers = Func2WorkList.at(&Func).at(CurrentPointerLevel);
            }
            for(auto Ptr : Pointers){
                markLabelsForPtr(Ptr);
            }

        }

        for(auto &Func : m.functions()){
            // dbgs() << getCurrentTime() << " Analyzing function: " 
            //     << Func.getName() << " with pointer level: " << CurrentPointerLevel << "\n";

            auto Pointers = std::set<const PointerTy*>{};
            if(Func2WorkList.count(&Func) && Func2WorkList[&Func].count(CurrentPointerLevel)){
                Pointers = Func2WorkList.at(&Func).at(CurrentPointerLevel);
            }
            for(auto Ptr : Pointers){
                auto Pair = buildDominatorGraph(&Func, Ptr);
                auto OUT = Pair.first;
                auto DG = Pair.second;
                auto UseLocs = getUseLocations(Ptr);
                buildDefUseGraph(UseLocs, Ptr, OUT, DG);
            }

            auto PropagateList = initializePropagateList(Pointers, CurrentPointerLevel, &Func);

            // dbgs() << getCurrentTime() << " Propagating function: " 
            //     << Func.getName() << " with pointer level: " << CurrentPointerLevel << "\n";
            // Also save caller when passing the arguments.
            propagate(PropagateList, &Func);

        }
        --CurrentPointerLevel;

    }

    auto stop = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(stop - start);
 
    // To get the value of duration use the count()
    // member function on the duration object
    dbgs() << duration.count() << "\n";


    DEBUG_WITH_TYPE("label", dumpLabelMap());
    DEBUG_WITH_TYPE("pts", dumpPointsToSet());

    DEBUG_WITH_TYPE("pts", dumpPointsToSet());
    dumpAliasMap();

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
                OS << *L.Ptr << ")";
            }
            else{
                OS << "nullptr)";
            }
        }
        else if(L.Type == Label::LabelType::Use){
            OS << "Use(";
            if(L.Ptr){
                OS << *L.Ptr << ")";
            }
            else{
                OS << "nullptr)";
            }
        }
        else if(L.Type == Label::LabelType::DefUse){
            OS << "DefUse(";
            if(L.Ptr){
                OS << *L.Ptr << ")";
            }
            else{
                OS << "nullptr)";
            }
        }
        return OS;
    }

}


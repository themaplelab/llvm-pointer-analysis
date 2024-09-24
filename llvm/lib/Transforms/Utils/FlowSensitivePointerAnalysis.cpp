#include "llvm/Transforms/Utils/FlowSensitivePointerAnalysis.h"

#include "llvm/IR/DerivedUser.h"
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
        dbgs() << "At program location " << Loc->getFunction()->getName() << *Loc << ":\n";
        for(auto PtsForPtr : PointsToSetOut.at(Loc)){
            if(!PtsForPtr.first){
                dbgs() << "\t" << "nullptr" << " ==>\n";
            }
            else if(dyn_cast<Argument>(PtsForPtr.first)){
                dbgs() << "\t" << *(PtsForPtr.first) << " ==>\n";
            }
            else{
                dbgs() << *(PtsForPtr.first) << " ==>\n";
            }
            
            for(auto Pointee : PtsForPtr.second){
                if(!Pointee){
                    dbgs() << "\t " << "nullptr" << "\n";
                }
                else if(dyn_cast<Instruction>(Pointee)){
                    dbgs() << "\t" << *Pointee << "\n";
                }
                else if(auto Func = dyn_cast<Function>(Pointee)){
                    dbgs() << "\t" << Func->getName() << "\n";
                }
                else{
                    dbgs() << "\t " << *Pointee << "\n";
                }
            }
        }
    }

    if(PointsToSetIn.count(Loc)){
        dbgs() << "(IN) At program location " << Loc->getFunction()->getName() << *Loc << ":\n";
        for(auto PtsForPtr : PointsToSetIn.at(Loc)){
            if(!PtsForPtr.first){
                dbgs() << "\t" << "nullptr" << " ==>\n";
            }
            else if(dyn_cast<Argument>(PtsForPtr.first)){
                dbgs() << "\t" << *(PtsForPtr.first) << " ==>\n";
            }
            else{
                dbgs() << *(PtsForPtr.first) << " ==>\n";
            }
            
            for(auto Pointee : PtsForPtr.second){
                if(!Pointee){
                    dbgs() << "\t " << "nullptr" << "\n";
                }
                else if(dyn_cast<Instruction>(Pointee)){
                    dbgs() << "\t" << *Pointee << "\n";
                }
                else if(auto Func = dyn_cast<Function>(Pointee)){
                    dbgs() << "\t" << Func->getName() << "\n";
                }
                else{
                    dbgs() << "\t " << *Pointee << "\n";
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
    dbgs() << "Print alias map stats\n";
    // C++26 will treat _ as a special value that does not cause unused warning.
    for(auto LocAndPtr : AliasMap){
           if(PointsToSetOut.count(LocAndPtr.first)){
            dbgs() << "At program location" << *LocAndPtr.first << ":\n";
            for(auto AliasForPtr : AliasMap.at(LocAndPtr.first)){
                dbgs() << *(AliasForPtr.first) << " alias to \n";
                for(auto Pointee : AliasForPtr.second){
                    if(!Pointee){
                        dbgs() << "\t " << "nullptr" << "\n";
                    }
                    else if(dyn_cast<Instruction>(Pointee)){
                        dbgs() << "\t" << *Pointee << "\n";
                    }
                    else{
                        dbgs() << "\t " << *Pointee << "\n";
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

    for(auto &Global : M.globals()){
        if(Global.getType()->isPointerTy()){
            auto PointerLevel = computePointerLevel(&Global);
            GlobalWorkList[PointerLevel].insert(&Global);
        }
    }

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

        for(auto &Global : Func->getParent()->globals()){
            if(!Global.getType()->isPointerTy()){
                continue;
            }
            addDefLabel(&Global, FirstInst, Func);
            if(Global.hasInitializer()){
                PointsToSetOut[FirstInst][&Global] = std::set<const Value*>{Global.getInitializer()};
            }
            else{
                PointsToSetOut[FirstInst][&Global] = std::set<const Value*>{nullptr};
            }
            
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
                DEBUG_WITH_TYPE("warning", dbgs() << getCurrentTime() << " WARNING:" 
                    << *Call << " performs an indirect call\n");
            }
            else{
                Caller2Callee[Func].insert(Call->getCalledFunction());
                size_t idx = 0;

                // for(auto &Global : Func->getParent()->globals()){
                //     addDefLabel(&Global, Call, Func);
                //     addUseLabel(&Global, Call);          
                // }
                
                for(auto Arg : Call->operand_values()){
                    auto Arg0 = Arg;
                    if(isa<BitCastInst>(Arg) || isa<GetElementPtrInst>(Arg)){
                        // dbgs() << "initialize" << "\n";
                        Arg0 = getOriginalPointer(Arg);
                    }
                    if(isa_and_nonnull<GlobalVariable>(Arg0)){
                        addDefLabel(Arg0, Call, Call->getFunction());
                        addUseLabel(Arg0, Call);
                    }
                    
                    if(Arg0 && !isa<LoadInst>(Arg0) && Arg0->getType()->isPointerTy()){
                        CallSite2ArgIdx[Call][Arg0].insert(idx);
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

/// @brief Mark def and use labels for pointer \p Ptr at user location of \p UserRoot. The labels are later 
/// used for building def use graph.
void FlowSensitivePointerAnalysis::markLabelsForPtrAtInstUsers(const PointerTy *Ptr, const PointerTy *UserRoot){

    DEBUG_WITH_TYPE("fspa", dbgs() << getCurrentTime() << " Marking labels for "
         << *Ptr << "\n");

    
    for(auto User : UserRoot->users()){
        
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
        else if(isa<GetElementPtrInst>(User) || isa<BitCastInst>(User)){
            markLabelsForPtrAtInstUsers(Ptr, User);          
        }
        else if(dyn_cast<CmpInst>(User) || dyn_cast<InvokeInst>(User) || dyn_cast<VAArgInst>(User) || 
                dyn_cast<PHINode>(User) || dyn_cast<PtrToIntInst>(User) || dyn_cast<SelectInst>(User) || 
                dyn_cast<DerivedUser>(User)){
                    // cannot find program location from a derived user. Do it alternatively in initialize
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

    if(isa<GetElementPtrInst>(Loc)){
        for(auto User : Loc->users()){
            if(auto PL = dyn_cast<ProgramLocationTy>(User)){
                Res.push_back(PL);
            }
        }
        return Res;
    }
    

    
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

    if(Func->isDeclaration()){
        return PropagateList;
    }

    for(auto Ptr: Pointers){
        
        if(auto Arg = dyn_cast<Argument>(Ptr)){
            auto FirstInst = Func->getEntryBlock().getFirstNonPHIOrDbg();
            auto InitialDUEdges = getAffectUseLocations(FirstInst, Arg);
            for(auto UseLoc : InitialDUEdges){
                PropagateList.insert(std::make_tuple(FirstInst, UseLoc, Arg));
            }
        }
        else if(auto Global = dyn_cast<GlobalVariable>(Ptr)){
            auto FirstInst = Func->getEntryBlock().getFirstNonPHIOrDbg();
            auto InitialDUEdges = getAffectUseLocations(FirstInst, Global);
            for(auto UseLoc : InitialDUEdges){
                PropagateList.insert(std::make_tuple(FirstInst, UseLoc, Global));
            }
        }
        else if(auto Loc = dyn_cast<ProgramLocationTy>(Ptr)){
            // An allocated pointer in llvm also represents its allocation location.
            auto InitialDUEdges = getAffectUseLocations(Loc, Ptr);
            for(auto UseLoc : InitialDUEdges){
                PropagateList.insert(std::make_tuple(Loc, UseLoc, Ptr));
            }
        }
        else{
            assert(false && "Cannot use nullptr as program location");
        } 
    }
    return PropagateList;
}

/// @brief Pass PTS(Ptr) from DefLoc to UseLoc, skip store instruction.
void FlowSensitivePointerAnalysis::propagatePointsToInformation(const ProgramLocationTy *UseLoc,
     const ProgramLocationTy *DefLoc, const PointerTy *Ptr){
        PointsToSetIn[UseLoc][Ptr];
        for(auto P : PointsToSetOut.at(DefLoc).at(Ptr)){
            if(!P){
                PointsToSetIn[UseLoc][Ptr].insert(P);
            }
            else if(!isa<LoadInst>(P)){
                PointsToSetIn[UseLoc][Ptr].insert(P);
            }
        }

    return;
}

const FlowSensitivePointerAnalysis::PointerTy* FlowSensitivePointerAnalysis::
    getOriginalPointer(const PointerTy *Ptr){
    
    if(!Ptr){
        return Ptr;
    }

    while(Ptr != Ptr->stripPointerCastsAndAliases()->stripInBoundsOffsets()){
        Ptr = Ptr->stripPointerCastsAndAliases()->stripInBoundsOffsets();
        
    }
    if(auto GEP = dyn_cast<GetElementPtrInst>(Ptr)){
        Ptr = getOriginalPointer(GEP->getPointerOperand());
    }

    return Ptr;

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
    // dbgs() << "getRealPointsToSet" << "\n";
    auto OriginalPtr = getOriginalPointer(ValueOperand);
    while(auto GEP = dyn_cast<GetElementPtrInst>(OriginalPtr)){
        OriginalPtr = GEP->getPointerOperand();
    }

    Pointees.insert(OriginalPtr);
    if(AliasMap.count(Loc) && AliasMap[Loc].count(OriginalPtr)){
        Pointees = AliasMap.at(Loc).at(OriginalPtr);
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

    // dbgs() << "jiaqi" << *Loc << "\n";

    assert(Pointer && "Cannot update PTS for nullptr");
    auto AliasSet = std::set<const PointerTy *>{};
    if(auto Store = dyn_cast<StoreInst>(Loc)){
        // dbgs() << "updatePointsToSet" << "\n";
        if(auto Load = dyn_cast<LoadInst>(getOriginalPointer(Store->getPointerOperand()))){ 
            // dbgs() << *Load << "\n";
            if(PointsToSetOut.count(Load) && PointsToSetOut[Load].count(Load->getPointerOperand())){
                for(auto Ptr : PointsToSetOut[Load][Load->getPointerOperand()]){
                    if(Ptr && !isa<LoadInst>(Ptr)){
                        AliasSet.insert(Ptr);
                    }
                }
            }

        }
        else if(auto Arg = dyn_cast<Argument>(getOriginalPointer(Store->getPointerOperand()))){
            AliasSet.insert(Arg);
        }
    }

    if(AliasSet.size() <= 1){
        if(AliasSet.empty() || !isa<Argument>(*AliasSet.begin())){
            // Strong update
            if(updatePointsToSetAtProgramLocation(Loc, Pointer, AdjustedPointsToSet)){
                for(auto UseLoc : getAffectUseLocations(Loc, Pointer)){    
                    PropagateList.insert(std::make_tuple(Loc, UseLoc, Pointer));
                }
            }
        }
        else{
            auto Arg = *AliasSet.begin();
            // dbgs() << "delimiter\n";
            // for(auto A : FuncPara2Alias[Loc->getFunction()][Arg]){
            //     dbgs() << *A << "\n";
            // }



            if(FuncPara2Alias[Loc->getFunction()][Arg].size() > 1){
                // dbgs() << "weak\n";
                // weak update for parameter
                // bug : not letting out = in
                PointsToSetOut[Loc][Arg] = PointsToSetIn[Loc][Arg];
                if(insertPointsToSetAtProgramLocation(Loc, Arg, AdjustedPointsToSet)){
                    for(auto UseLoc : getAffectUseLocations(Loc, Arg)){    
                        PropagateList.insert(std::make_tuple(Loc, UseLoc, Arg));
                    }   
                }
            }
            else{
                // Strong update for parameter
                if(updatePointsToSetAtProgramLocation(Loc, Arg, AdjustedPointsToSet)){
                    for(auto UseLoc : getAffectUseLocations(Loc, Arg)){    
                        PropagateList.insert(std::make_tuple(Loc, UseLoc, Arg));
                    }
                }
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
    getAlias(const ProgramLocationTy *Loc, const PointerTy *Ptr){

    if(auto Load = dyn_cast<LoadInst>(Ptr)){
        if(AliasMap.count(Loc) && AliasMap[Loc].count(Load->getPointerOperand())){
        return AliasMap.at(Loc).at(Load->getPointerOperand());
        }
        else{
            return std::set<const PointerTy*>{Load->getPointerOperand()};
        }
    }
    else{
        return std::set<const PointerTy*>{Ptr};
    }

    
}

std::set<const User*> FlowSensitivePointerAnalysis::includeBitCastAndGEPUsers(const User *U){

    std::set<const User*> Res;

    for(auto User : U->users()){
        if(auto BC = dyn_cast<BitCastInst>(User)){
            auto UseLocs = includeBitCastAndGEPUsers(User);
            Res.insert(UseLocs.begin(), UseLocs.end());
        }
        else if(auto GEP = dyn_cast<GetElementPtrInst>(User)){
            if(GEP->getPointerOperand() != U){
                continue;
            }
            auto UseLocs = includeBitCastAndGEPUsers(User);
            Res.insert(UseLocs.begin(), UseLocs.end());
        }
        else{
            Res.insert(User);
        }
    }
    return Res;
}

/// @brief Update the alias set of pointer x introduced by a \p loadInst 'x = load y'.
void FlowSensitivePointerAnalysis::updateAliasInformation(const ProgramLocationTy *Loc, const PointerTy *Ptr){
    
    auto Aliases = getAlias(Loc, Ptr);
    for(auto &Alias : Aliases){
        if(!Alias){
            continue;
        }
        assert(Alias && "Cannot process nullptr");

        // if we have x = load y, and y alias z, we will need pts(z) at current program location.
        if(!AliasUser.count(Alias)){
            for(auto User : Alias->users()){
                if(isa<BitCastInst>(User) || isa<GetElementPtrInst>(User)){
                    if(auto GEP = dyn_cast<GetElementPtrInst>(User)){
                        if(Alias != GEP->getPointerOperand()){
                            continue;
                        }
                    }
                    auto UseLocs = includeBitCastAndGEPUsers(User);
                    AliasUser[Alias].insert(UseLocs.begin(), UseLocs.end());
                }
                else{
                    AliasUser[Alias].insert(User);
                }
            }
        }
        AliasUser[Alias].insert(Loc);
        if(PointsToSetOut.count(Loc) && PointsToSetOut[Loc].count(Alias)){
            // bug: should be assign not insert
            AliasMap[Loc][Ptr].insert(PointsToSetOut.at(Loc).at(Alias).begin(), PointsToSetOut.at(Loc).at(Alias).end());
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

    if(!AliasUser.count(Loc)){
        dbgs() << *Loc << "\n";
    }
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
            // dbgs() << "updateAliasUsers" << "\n";
            if(Ptr == getOriginalPointer(Store->getPointerOperand())){ 
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
            else if(Ptr == getOriginalPointer(Store->getValueOperand())){
                
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
                raw_string_ostream(Str) << "Hitting at " << *Store << " with pointer " << *Loc << " " 
                    << *getOriginalPointer(Store->getPointerOperand()) << "\n";
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
        else if(auto Call = dyn_cast<CallInst>(UseLoc)){
            if(!Call->getCalledFunction() || Call->getCalledFunction()->isDeclaration() || !Ptr->getType()->isPointerTy()){
                // Ignore indirect call.
                continue;
            }
            // Find corresponding parameter index from the actual argument.
            size_t ArgumentIdx = 0;
            for(auto Arg : Call->operand_values()){
                if(getOriginalPointer(Arg) == Ptr){
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

            

        // }
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

        // dbgs() << getCurrentTime() << " Propagating edge " << *DefLoc << " === " << *Ptr << " ===> " << *UseLoc << "\n";

        propagatePointsToInformation(UseLoc, DefLoc, Ptr);

        if(auto Store = dyn_cast<StoreInst>(UseLoc)){
            // add the case of c = call f x
            auto PTS = getRealPointsToSet(UseLoc, Store->getValueOperand());
            updatePointsToSet(UseLoc, Ptr, PTS, PropagateList);
            
            if(isa<Argument>(Ptr)){
                // get first inst of the function
                auto FirstInst = UseLoc->getParent()->getParent()->getEntryBlock().getFirstNonPHIOrDbg();
                // for each arg that alias to para
                // update its pts

                // if more than 1 pointer alias to Ptr, perform weak update.
                // else perform strong update
                if(AliasMap[FirstInst][Ptr].size() <= 1){
                    for(auto Alias : AliasMap[FirstInst][Ptr]){
                        if(updatePointsToSetAtProgramLocation(UseLoc, Alias, PTS)){
                            for(auto UL : getAffectUseLocations(UseLoc, Alias)){    
                                PropagateList.insert(std::make_tuple(UseLoc, UL, Alias));
                            }
                        }
                    }
                }
                else{
                    for(auto Alias : AliasMap[FirstInst][Ptr]){
                        PointsToSetOut[UseLoc][Alias] = PointsToSetIn[UseLoc][Alias];
                        if(insertPointsToSetAtProgramLocation(UseLoc, Alias, PTS)){
                            for(auto UL : getAffectUseLocations(UseLoc, Alias)){    
                                PropagateList.insert(std::make_tuple(UseLoc, UL, Alias));
                            }
                        }

                    }
                }

                
            }
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
                    for(auto User : UseLoc->users()){
                        // AliasUser[UseLoc].insert(user);

                        if(isa<BitCastInst>(User) || isa<GetElementPtrInst>(User)){
                            if(auto GEP = dyn_cast<GetElementPtrInst>(User)){
                                if(UseLoc != GEP->getPointerOperand()){
                                    continue;
                                }
                            }
                            auto UseLocs = includeBitCastAndGEPUsers(User);
                            AliasUser[UseLoc].insert(UseLocs.begin(), UseLocs.end());
                        }
                        else{
                            AliasUser[UseLoc].insert(User);
                        }
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
            // dbgs() << ArgumentIdxs.size() << "\n";
            for(auto ArgumentIdx : ArgumentIdxs){
                assert(ArgumentIdx < Call->arg_size() && "Arguemnt idx out of bound.");
                // dbgs() << *Ptr << "\n";
                updateArgPointsToSetOfFunc(Call->getCalledFunction(), PointsToSetIn.at(UseLoc).at(Ptr), ArgumentIdx, PropagateList);
                // update alias set of para
                // call f x passing pts(x) to here now.
                updateArgAliasSetOfFunc(Call->getCalledFunction(), Call->getFunction(), Ptr, ArgumentIdx, PropagateList);
            }

        }
        else if(auto Return = dyn_cast<ReturnInst>(UseLoc)){

            PointsToSetOut[UseLoc][Ptr] = PointsToSetIn.at(UseLoc).at(Ptr);
            // Propagate ptr parameter.
            if(isa<Argument>(Ptr) && Ptr->getType()->isPointerTy()){
                size_t ParaIdx = 0;
                for(auto &Arg : Return->getFunction()->args()){
                    if(&Arg == Ptr){
                        break;
                    }
                    ++ParaIdx;
                }

                for(auto CallSite : Func2CallerLocation[Return->getFunction()]){
                    const Value *Arg = CallSite->getOperand(ParaIdx);
                    if(isa<BitCastInst>(Arg) || isa<GetElementPtrInst>(Arg)){
                        Arg = getOriginalPointer(Arg);
                    }

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
            else if(Ptr->getType()->isPointerTy() && Ptr == getOriginalPointer(Return->getReturnValue())){
                // propagate return ptr
                for(auto CallSite : Func2CallerLocation[Return->getFunction()]){
                    auto OldAliasSet = std::set<const PointerTy*>{};
                    if(AliasMap.count(CallSite) && AliasMap[CallSite].count(CallSite)){
                        OldAliasSet = AliasMap.at(CallSite).at(CallSite);
                    }

                    updateAliasInformation(CallSite, CallSite);

                    auto NewAliasSet = std::set<const PointerTy*>{};
                    if(AliasMap.count(CallSite) && AliasMap[CallSite].count(CallSite)){
                        NewAliasSet = AliasMap.at(CallSite).at(CallSite);
                    }

                    if(OldAliasSet != NewAliasSet){
                        if(!AliasUser.count(CallSite)){
                            // Create empty entry
                            AliasUser[CallSite];
                            for(auto User : CallSite->users()){
                                // AliasUser[CallSite].insert(user);

                                if(isa<BitCastInst>(User) || isa<GetElementPtrInst>(User)){
                                    if(auto GEP = dyn_cast<GetElementPtrInst>(User)){
                                        if(CallSite != GEP->getPointerOperand()){
                                            continue;
                                        }
                                    }
                                    auto CallSites = includeBitCastAndGEPUsers(User);
                                    AliasUser[CallSite].insert(CallSites.begin(), CallSites.end());
                                }
                                else{
                                    AliasUser[CallSite].insert(User);
                                }
                            }
                        }
                        updateAliasUsers(CallSite, PropagateList);
                    }
                }

            }
        }



        PropagateList.erase(PropagateList.begin());
    }
    return;
}


void FlowSensitivePointerAnalysis::updateArgAliasSetOfFunc(const Function *Func, const Function *F, const PointerTy *Ptr,
    size_t ArgIdx, SetVector<DefUseEdgeTupleTy> &PropagateList){

    // dbgs() << "UAASF " << Func->getName() << " " << F->getName() << " " << *Ptr << "\n";

    const Value *Parameter;
    for(auto &Para : Func->args()){
        Parameter = &Para;
        if(!ArgIdx){
            break;
        }
        --ArgIdx;
    }

    if(isa<Argument>(Ptr)){
        auto OldSz = FuncPara2Alias[Func][Parameter].size();
        FuncPara2Alias[Func][Parameter].insert(FuncPara2Alias[F][Ptr].begin(), FuncPara2Alias[F][Ptr].end());
        if(OldSz != FuncPara2Alias[Func][Parameter].size()){
            auto FirstInst = Func->getEntryBlock().getFirstNonPHIOrDbg();
            // for(auto UseLoc : getAffectUseLocations(FirstInst, Parameter)){    
            //     PropagateList.insert(std::make_tuple(FirstInst, UseLoc, Parameter));
            // }
        }
    }
    else{
        auto Res = FuncPara2Alias[Func][Parameter].insert(Ptr);
        if(Res.second){
            auto FirstInst = Func->getEntryBlock().getFirstNonPHIOrDbg();
            // for(auto UseLoc : getAffectUseLocations(FirstInst, Parameter)){    
            //     PropagateList.insert(std::make_tuple(FirstInst, UseLoc, Parameter));
            // }
        }
    }
    

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
    
    
    while(CurrentPointerLevel > 1){
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
            if(GlobalWorkList.count(CurrentPointerLevel)){
                Pointers.insert(GlobalWorkList[CurrentPointerLevel].begin(), GlobalWorkList[CurrentPointerLevel].end());
            }
            for(auto Ptr : Pointers){
                markLabelsForPtrAtInstUsers(Ptr, Ptr);
            }

        }

        for(auto &Func : m.functions()){
            // dbgs() << getCurrentTime() << " Analyzing function: " 
            //     << Func.getName() << " with pointer level: " << CurrentPointerLevel << "\n";

            auto Pointers = std::set<const PointerTy*>{};
            if(Func2WorkList.count(&Func) && Func2WorkList[&Func].count(CurrentPointerLevel)){
                Pointers = Func2WorkList.at(&Func).at(CurrentPointerLevel);
            }
            if(GlobalWorkList.count(CurrentPointerLevel)){
                Pointers.insert(GlobalWorkList[CurrentPointerLevel].begin(), GlobalWorkList[CurrentPointerLevel].end());
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


    // dumpLabelMap();

    // dumpPointsToSet();

    // dumpAliasMap();

    // for(auto Pair : AliasUser){
    //     dbgs() << *Pair.first << " is used at\n";
    //     for(auto U : Pair.second){
    //         dbgs() << "\t" << *U << "\n";
    //     }
    // }

    // for(auto Pair : CallSite2ArgIdx){
    //     dbgs() << *Pair.first <<" CALLSITE \n";
    //     for(auto P : Pair.second){
    //         if(P.first){
    //             dbgs() << "Arg " << *P.first << "\n";
    //         }
    //         else{
    //             dbgs() << "Arg nullptr" << "\n";
    //         }
            
    //         for(auto I : P.second){
    //             dbgs() << I << "\n";
    //         }
    //     }
    // }

    // for(auto Pair : FuncPara2Alias){
    //     dbgs() << "Function " << Pair.first->getName() << "\n";
    //     for(auto P : Pair.second){
    //         dbgs() << "Parameter " << *P.first << "\n";
    //         for(auto A : P.second){
    //             dbgs() << *A << "\n";
    //         }
    //     }
    // }

    AnalysisResult.setFunc2Pointers(Func2AllocatedPointersAndParameterAliases);
    AnalysisResult.setPointsToSet(PointsToSetOut);

    size_t TotalPtsSize = 0, NumPts = 0, MaxSize = 0, MinSize = 1000;
    
    const ProgramLocationTy *Loc = nullptr;
    for(auto Pair : PointsToSetOut){
        if(!isa<StoreInst>(Pair.first) && !isa<LoadInst>(Pair.first)){
            continue;
        }
        NumPts += 1;
        size_t LocalMax = 0;
        for(auto P : Pair.second){
            if(LocalMax < P.second.size()){
                LocalMax = P.second.size();
            }
            
            if(P.second.size() > MaxSize){
                MaxSize = P.second.size();
                Loc = Pair.first;
            }
            if(P.second.size() < MinSize){
                MinSize = P.second.size();
            }
            
        }
        TotalPtsSize += LocalMax;
    }

    dbgs() << "End of analysis. Avg Pts Size is " << (double)TotalPtsSize / NumPts << " " << MaxSize << " " << MinSize << "\n";
    dbgs() << *Loc << "\n";


    // count dug nodes number and edge numbers.
    size_t TotalEdges = 0;
    std::set<const ProgramLocationTy*> Nodes;

    for(auto Pair : DefUseGraph){
        Nodes.insert(Pair.first);
        for(auto P : Pair.second){
            for(auto E : P.second){
                Nodes.insert(E);
                ++TotalEdges;
            }
        }
    }

    dbgs() << "Total number of nodes: " << Nodes.size() << ", and total number of edges is " << TotalEdges << "\n";

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


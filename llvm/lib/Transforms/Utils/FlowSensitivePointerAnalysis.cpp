#include "llvm/Transforms/Utils/FlowSensitivePointerAnalysis.h"

#include "llvm/Support/Debug.h"
#include "llvm/Support/Timer.h"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <sstream>


// 1. some def use edge are found more than once.

using namespace llvm;

// std::set<const FlowSensitivePointerAnalysis::ProgramLocationTy*> FlowSensitivePointerAnalysis::
//     findDefFromUseByDom(const ProgramLocationTy *Loc, const PointerTy *Ptr){
//         // find all defs in current function.
//         auto CurrentFunction = Loc->getFunction();
//         std::set<ProgramLocationTy*> DefsInCurrentFunction{};

//         for(auto DefLoc : DefList[Ptr]){
//             if(DefLoc->getFunction() == CurrentFunction){
//                 DefsInCurrentFunction.insert(DefLoc);
//             }
//         }

//         // find all dominators and d such that Loc \in DF(d)
//         std::set<const BasicBlock*> IDF{};
//         std::set<BasicBlock*> WorkList{};

//         for(auto DefLoc : DefsInCurrentFunction){
//             WorkList.insert(DefLoc->getParent());
//         }

//         while(!WorkList.empty()){
//             auto BBIter = WorkList.begin();
//             IDF.insert(*BBIter);
//             auto DF = Func2DF[CurrentFunction]->find(*BBIter)->second;
//             for(auto Node : DF){
//                 if(!IDF.count(Node)){
//                     WorkList.insert(Node);
//                 }
//             }
//             WorkList.erase(BBIter);
//         }

//         // For every node in current function, at least one node in IDF will dominate it.

//         // Find immediate dominator for useloc in IDF.
//         std::set<const BasicBlock*> Doms{};
//         for(auto BB : IDF){
//             if(Func2DT[CurrentFunction]->dominates(BB, Loc->getParent())){
//                 Doms.insert(BB);
//             }
//         }

//         auto IDOM = *Doms.begin();
//         for(auto BB : IDF){
//             if(Func2DT[CurrentFunction]->findNearestCommonDominator(IDOM, BB) == IDOM){
//                 IDOM = BB;
//             }
//         }

// }


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

    if(PointsToSet.count(Loc)){
        DEBUG_WITH_TYPE("pts", dbgs() << "At program location" << *Loc << ":\n");
        for(auto PtsForPtr : PointsToSet.at(Loc)){
            DEBUG_WITH_TYPE("pts", dbgs() << *(PtsForPtr.first) << " ==>\n");
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
    for(auto PtsForPtr : PointsToSet){
        printPointsToSetAtProgramLocation(PtsForPtr.first);
    }
}

void FlowSensitivePointerAnalysis::dumpAliasMap(){
    DEBUG_WITH_TYPE("alias", dbgs() << "Print alias map stats\n");
    // C++26 will treat _ as a special value that does not cause unused warning.
    for(auto LocAndPtr : AliasMap){
           if(PointsToSet.count(LocAndPtr.first)){
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
    size_t PtrLvl = 0;
    for(auto &Func : M.functions()){
        ++TotalFunctionNumber;
        initialize(&Func);
        if(PtrLvl < Func2WorkList.at(&Func).size()){
            PtrLvl = Func2WorkList.at(&Func).size();
        }
    }

    assert(PtrLvl >= 0 && "Pointer level cannot be negative.");

    AnalysisResult.setWorkList(Func2WorkList);
    TotalFunctionNumber *= PtrLvl;
    return PtrLvl;
}

/// @brief Compute the pointer level of an allocated pointer.
/// @return Pointer level for \p Ptr.
size_t FlowSensitivePointerAnalysis::computePointerLevel(const PointerTy *Ptr){
    size_t PointerLevel = 1;

    auto ty = Ptr->getType();
    while(ty->getPointerElementType()->isPointerTy()){
        ++PointerLevel;
        ty = ty->getPointerElementType();
    }
    return PointerLevel;
}

/// @brief Calculate pointer level for function \p Func. Mark labels for each pointer
///     related instructions. Store pointers into worklist according to their pointer level.
void FlowSensitivePointerAnalysis::initialize(const Function *Func){

    DEBUG_WITH_TYPE("fspa", dbgs() << getCurrentTime() << " Initializing function "
         << Func->getName() << "\n");

    WorkListTy WorkList;

    for(auto &Inst : instructions(*Func)){
        if(const AllocaInst *Alloca = dyn_cast<AllocaInst>(&Inst)){
            auto PointerLevel = computePointerLevel(Alloca);
            WorkList[PointerLevel].insert(Alloca);
            LabelMap[Alloca].insert(Label(Alloca, Label::LabelType::Def));
            Def2Functions[Alloca].insert(Alloca->getFunction());

            // A -> nullptr means A is not initialized. It helps us to find dereference of nullptr.
            PointsToSet[&Inst][Alloca] = std::set<const Value*>{nullptr};
        }
        else if(const CallInst *Call = dyn_cast<CallInst>(&Inst)){
            Func2CallerLocation[Call->getCalledFunction()].insert(Call);
            if(!Call->getCalledFunction()){

                DEBUG_WITH_TYPE("warning", dbgs() << getCurrentTime() << " WARNING:" 
                    << *Call << " performs an indirect call\n");
            }
            else{
                Caller2Callee[Func].insert(Call->getCalledFunction());
            }
            
        }
        else if(const ReturnInst *Return = dyn_cast<ReturnInst>(&Inst)){
            Func2TerminateBBs[Func].insert(Return->getParent());
        }
    }

    Func2WorkList.emplace(Func, WorkList);
    return;
}

/// @brief Propagate points-to set for a pointer if it is not defined at a
///     program location
void FlowSensitivePointerAnalysis::populatePointsToSet(Module &M){


    for(auto &Func : M.functions()){

        if(Func.isDeclaration()){
            continue;
        }

        DEBUG_WITH_TYPE("fspa", dbgs() << getCurrentTime() << " Populate points-to set for function "
            << Func.getName() << "\n");

        SetVector<const PointerTy*> AllocatedPointersAndParameterAliases{};

        // Collect all allocated pointer in current function.
        if(Func2WorkList.count(&Func)){
            for(auto WorkList : Func2WorkList.at(&Func)){
                AllocatedPointersAndParameterAliases.insert(WorkList.second.begin(), WorkList.second.end());
            }
        }
        // Collect all pointers that alias to parameters of this function.
        if(FuncParas2AliasSet.count(&Func)){
            for(auto Paras2AliasSet : FuncParas2AliasSet[&Func]){
                AllocatedPointersAndParameterAliases.insert(Paras2AliasSet.second.begin(), Paras2AliasSet.second.end());
            }
        }
        Func2AllocatedPointersAndParameterAliases[&Func] = AllocatedPointersAndParameterAliases;

        populatePTSForFunc(&Func);

    }
}


void FlowSensitivePointerAnalysis::populatePTSForFunc(const Function *Func){
    DenseSet<const ProgramLocationTy*> Visited{};
    for(auto &Inst : instructions(Func)){
        populatePTSAtLocation(&Inst);
    }
    
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

/// @brief Find all possible next program location of program location
///     \p Loc inter-procedurally. If we run in to a function call, we will take
///     all return instructions of the function as result. If we already at the 
///     first instruction of the function, we will return all program locations
///     one instruction before the calling instruction that calls this function.
/// @return A set of program locations that possible be the next location of \p Loc.
std::set<const FlowSensitivePointerAnalysis::ProgramLocationTy*> FlowSensitivePointerAnalysis::
    getNextProgramLocations(const ProgramLocationTy *Loc, const Function *MadeFrom){

    DEBUG_WITH_TYPE("debug", dbgs() << getCurrentTime() << " Getting next loc for " 
        << Loc->getFunction()->getName() << "::" << Loc->getParent()->getName() << "::" << *Loc << "\n");

    std::set<const ProgramLocationTy*> res{};

    auto Next = Loc->getNextNonDebugInstruction();
    if(Next){
        if(auto Call = dyn_cast<CallInst>(Next)){
            // dbgs() << Call->getCalledFunction()->isDeclaration() << "\n";
            if(Call->getCalledFunction() != MadeFrom && Call->getCalledFunction() && !Call->getCalledFunction()->isDeclaration()){
                // should get first instruction of the function.
                res.insert(Call->getCalledFunction()->getEntryBlock().getFirstNonPHIOrDbg());
                return res;
            }
        }
        
        return std::set<const ProgramLocationTy*>{Next};
 
    }
    else{
        auto NextBasicBlocksRange = successors(Loc->getParent());
            
        if(NextBasicBlocksRange.empty()){
            if(Func2CallerLocation.count(Loc->getParent()->getParent())){
                for(auto CallLoc : Func2CallerLocation.at(Loc->getParent()->getParent())){
                    auto P = getNextProgramLocations(CallLoc, Loc->getFunction());
                    res.insert(P.begin(), P.end());
                }
            }
            return res;
        }
        else{
            for(auto *NextBasicBlock : NextBasicBlocksRange){
                auto N = NextBasicBlock->getFirstNonPHIOrDbg();
                if(auto Call = dyn_cast<CallInst>(N)){
                    // dbgs() << Call->getCalledFunction()->isDeclaration() << "\n";
                    if(Call->getCalledFunction() != MadeFrom && Call->getCalledFunction() && !Call->getCalledFunction()->isDeclaration()){
                        // should get first instruction of the function.
                        res.insert(Call->getCalledFunction()->getEntryBlock().getFirstNonPHIOrDbg());
                    }
                    else{
                        res.insert(N);
                    }
                }
                else{
                    res.insert(N);
                }
                
            }
            return res;
        }
    }
    
    
}

/// @brief Populate all points-to set in \p PassedPTS to program location \p Loc
void FlowSensitivePointerAnalysis::populatePTSAtLocation(const ProgramLocationTy *Loc){


    for(auto Pointer : Func2AllocatedPointersAndParameterAliases[Loc->getFunction()]){
        // If program location Loc defines points-to set of Pointer, we make sure
        // no intermediate pointer is removed.
        if(PointsToSet.count(Loc) && PointsToSet[Loc].count(Pointer)){
            for(auto it = PointsToSet[Loc][Pointer].begin(); it != PointsToSet[Loc][Pointer].end();){
                if(*it && dyn_cast<LoadInst>(*it)){
                    it = PointsToSet[Loc][Pointer].erase(it);
                }
                else{
                    ++it;
                }
            }
        }
        else{
            if(Loc == &Loc->getFunction()->getEntryBlock().front()){
                break;
            }
            auto Defs = findDefFromPrevOfUseLoc(Loc, Pointer);
            for(auto Def : Defs){
                dbgs() << "Found def " << *Def << "\n";
                for(auto Ptr : PointsToSet[Def][Pointer]){
                    if(!dyn_cast_or_null<LoadInst>(Ptr)){
                        PointsToSet[Loc][Pointer].insert(Ptr);
                    }
                }
                // PointsToSet[Loc][Pointer].insert(PointsToSet[Def][Pointer].begin(), PointsToSet[Def][Pointer].end());
            }  
        }
    }
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
            LabelMap[Store].insert(Label(Ptr, Label::LabelType::Def));
            LabelMap[Store].insert(Label(Ptr, Label::LabelType::Use));
            Def2Functions[Ptr].insert(Store->getFunction());
            UseList[Ptr].insert(Store);
            DefList[Ptr].insert(Store);
        }
        else if(auto *Load = dyn_cast<LoadInst>(User)){
            LabelMap[Load].insert(Label(Ptr, Label::LabelType::Use));
            UseList[Ptr].insert(Load);
        }
        else if(auto *Call = dyn_cast<CallInst>(User)){
            LabelMap[Call].insert(Label(Ptr, Label::LabelType::Use));
            UseList[Ptr].insert(Call);
        }
        else if(auto *Ret = dyn_cast<ReturnInst>(User)){
            LabelMap[Ret].insert(Label(Ptr, Label::LabelType::Use));
            UseList[Ptr].insert(Ret);
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
void FlowSensitivePointerAnalysis::buildDefUseGraph(std::set<const ProgramLocationTy*> UseLocs, const PointerTy *Ptr){
    for(auto UseLoc : UseLocs){
        DEBUG_WITH_TYPE("fspa", dbgs() << getCurrentTime() << " Building def-use graph for " 
            << *Ptr << " at " << *UseLoc << "\n");

        auto DefLocs = findDefFromPrevOfUseLoc(UseLoc, Ptr);
        for(auto Def : DefLocs){
            addDefUseEdge(Def, UseLoc, Ptr);
        }
    }
}

/// @brief Find all possible previous program location of program location
///     \p Loc inter-procedurally. If we run in to a function call, we will take
///     all return instructions of the function as result. If we already at the 
///     first instruction of the function, we will return all program locations
///     one instruction before the calling instruction that calls this function.
/// @return A set of program locations that possible be the previous location of \p Loc.
std::set<const FlowSensitivePointerAnalysis::ProgramLocationTy*>  
    FlowSensitivePointerAnalysis::getPrevProgramLocations(const ProgramLocationTy *Loc){

    std::set<const ProgramLocationTy*> res{};

    auto Prev = Loc->getPrevNonDebugInstruction();
    if(Prev){
        return std::set<const ProgramLocationTy*>{Prev};
    }
    else{
        auto PrevBasicBlocksRange = predecessors(Loc->getParent());
            
        if(PrevBasicBlocksRange.empty()){
            // bug: we need to perform two different operations in two cases:
            // 1. we finished a function and need to go back to the call location
            // 2. we finished a function but does not need to return to a specific location.
            // The first case can be recognized by a callstack. Everytime we get into a function,
            // we push the stack, and when we need to return, we check the callstack. If it is not
            // empty, it means we need to get back to the last element of the callstack. Otherwise,
            // we will go to all call locs.
            if(Func2CallerLocation.count(Loc->getParent()->getParent())){
                for(auto callLoc : Func2CallerLocation.at(Loc->getParent()->getParent())){
                    auto P = getPrevProgramLocations(callLoc);
                    res.insert(P.begin(), P.end());
                }
            }
            return res;
        }
        else{
            for(auto *PrevBasicBlock : PrevBasicBlocksRange){
                res.insert(&(PrevBasicBlock->back()));
            }
            return res;
        }
    }

}

/// @brief Find corresponding def labels at \p Loc. Avoid stucking in loops by
///     checking if a program location is already visited.
std::set<const FlowSensitivePointerAnalysis::ProgramLocationTy*> FlowSensitivePointerAnalysis::findDefFromUseLoc(
    const ProgramLocationTy *Loc, const PointerTy *Ptr, std::set<const ProgramLocationTy*> &Visited){

        // this func should also have a callstack.

    if(Visited.count(Loc)){
        return std::set<const ProgramLocationTy*>{};
    }
    Visited.insert(Loc);

    DEBUG_WITH_TYPE("dfg", dbgs() << getCurrentTime() << " Finding defs of " 
        << *Ptr << " at program location " << Loc->getFunction()->getName() 
        << "::" << Loc->getParent()->getName() << "::" << *Loc << "\n");

    if(hasDef(Loc, Ptr)){
        return std::set<const ProgramLocationTy*>{Loc};
    }
    if(DefLoc.count(Loc) && DefLoc.at(Loc).count(Ptr)){
        return DefLoc.at(Loc).at(Ptr);
    }

    std::set<const ProgramLocationTy*> Res{};

    // bug. If we trace function f and reach g (f calls g), we want to get back to f after searching g
    // instead of go to all callloc that calls g.
    auto Prevs = getPrevProgramLocations(Loc);
    for(auto Prev : Prevs){

        if(auto Call = dyn_cast<CallInst>(Prev)){
            if(Call->getCalledFunction() != Call->getFunction() && Call->getCalledFunction()){
                auto Defs = findDefFromFunc(Call->getCalledFunction(), Call, Visited, std::vector<const ProgramLocationTy*>{});
                Res.insert(Defs.begin(), Defs.end());
                continue;
            }
        }

        auto Defs = findDefFromUseLoc(Prev, Ptr, Visited);
        Res.insert(Defs.begin(), Defs.end());
    }

    return Res;
}

/// @brief Find all program locations that defined pointer \p Ptr and dominates 
///     program location \p Loc.
std::set<const FlowSensitivePointerAnalysis::ProgramLocationTy*> FlowSensitivePointerAnalysis::findDefFromPrevOfUseLoc(
    const ProgramLocationTy *Loc, const PointerTy *Ptr){

    DEBUG_WITH_TYPE("dfg", dbgs() << getCurrentTime() << " Finding defs of " 
        << *Ptr << " at program location " << Loc->getFunction()->getName() 
        << "::" << Loc->getParent()->getName() << "::" << *Loc << "\n");

    if(DefLoc.count(Loc) && DefLoc[Loc].count(Ptr)){
        return DefLoc.at(Loc).at(Ptr);
    }
    

    std::set<const ProgramLocationTy*> Res{};
    std::set<const ProgramLocationTy*> Visited{};
    std::vector<const ProgramLocationTy*> CallStack{};

    // If we trace the def locations from a use location in bottom-up order,
    // we might end up in loop searching, if we search function F but have no luck.
    // For example,
    // void F(){
    //  store x y
    //  G();    <= here
    // }
    // If we just finished searching G and try to find the previous program location
    // of G's first instruction, we want to skip G's body and return 'store x y' as 
    // the result. However, if the recursive call is not made from G, we want to look
    // into G's function body.

    Res = findDefFromInst(Loc, Ptr, Visited, CallStack, true);

    DefLoc[Loc][Ptr] = Res;
    return Res;
}

std::set<const FlowSensitivePointerAnalysis::ProgramLocationTy*> FlowSensitivePointerAnalysis::
    findDefFromFunc(const Function *Func, const PointerTy *Ptr, 
    std::set<const ProgramLocationTy *> &Visited, std::vector<const ProgramLocationTy*> CallStack){


    // Skip indirect call or external call or recursive call.
    if(!Func || (!CallStack.empty() && CallStack.back()->getFunction() == Func)){
        return std::set<const ProgramLocationTy*>{};
    }

    std::set<const ProgramLocationTy*> Res{};

    auto TerminatedBBs = std::set<const BasicBlock*>{};
    if(Func2TerminateBBs.count(Func)){
        TerminatedBBs = Func2TerminateBBs.at(Func);
        for(auto TBB : TerminatedBBs){
            assert(TBB && "Cannot get the last instruction of nullptr.");
            auto Defs = findDefFromInst(&(TBB->back()), Ptr, Visited, CallStack);
            Res.insert(Defs.begin(), Defs.end());
        }

        return Res;
    }
}



std::set<const FlowSensitivePointerAnalysis::ProgramLocationTy*> FlowSensitivePointerAnalysis::
    findDefFromInst(const ProgramLocationTy *Loc, const PointerTy *Ptr, std::set<const ProgramLocationTy *> &Visited,
    std::vector<const ProgramLocationTy*> CallStack, bool SkipSelf){

    if(Visited.count(Loc)){
        return std::set<const ProgramLocationTy*>{};
    }

    Visited.insert(Loc);
    
    DEBUG_WITH_TYPE("dfg", dbgs() << getCurrentTime() << " Finding defs of " 
        << *Ptr << " at program location " << Loc->getFunction()->getName() 
        << "::" << Loc->getParent()->getName() << "::" << *Loc << "\n");

    if(hasDef(Loc, Ptr) && !SkipSelf){
        return std::set<const ProgramLocationTy*>{Loc};
    }
    if(DefLoc.count(Loc) && DefLoc.at(Loc).count(Ptr)){
        dbgs() << "Found entry\n";
        return DefLoc.at(Loc).at(Ptr);
    }

    auto Prev = Loc->getPrevNonDebugInstruction();
    if(Prev){
        if(auto Call = dyn_cast<CallInst>(Prev)){
            // If prev is a call, we will search defs in that function.
            // The last argument marks where we should go after searching the function but some path 
            // still not has dominating def.
            if(Call->getFunction() == Call->getCalledFunction() || !Call->getCalledFunction()){
                return findDefFromInst(Prev, Ptr, Visited, CallStack);
            }
            CallStack.push_back(Call);      
            return findDefFromFunc(Call->getCalledFunction(), Ptr, Visited, CallStack);
        }
        else{
            // otherwise, it is a normal insturction, and we recursively search.
            return findDefFromInst(Prev, Ptr, Visited, CallStack);
        }
    }
    else{
        // we are reaching top of bb. Either we go pred bbs or go to calling context.
        std::set<const ProgramLocationTy*> Res{};
        auto PredBBRange = predecessors(Loc->getParent());
        if(PredBBRange.empty()){
            if(!CallStack.empty()){
                auto ReturnCallLoc = CallStack.back();
                CallStack.pop_back();
                return findDefFromInst(ReturnCallLoc, Ptr, Visited, CallStack);
            }

            if(Func2CallerLocation.count(Loc->getParent()->getParent())){
                for(auto CallLoc : Func2CallerLocation.at(Loc->getParent()->getParent())){
                    auto Defs = findDefFromInst(CallLoc, Ptr, Visited, CallStack);
                    Res.insert(Defs.begin(), Defs.end());
                }
            }
            return Res;
        }
        else{
            for(auto *PrevBasicBlock : PredBBRange){
                auto Defs = findDefFromInst(&(PrevBasicBlock->back()), Ptr, Visited, CallStack);
                Res.insert(Defs.begin(), Defs.end());
            }
            return Res;
        }
    }
}



/// @brief Build def use graph for all global variables of pointer level \p PtrLvl
void FlowSensitivePointerAnalysis::processGlobalVariables(size_t PtrLvl){

    if(GlobalWorkList.count(PtrLvl)){
        for(auto GlobalPtr : GlobalWorkList.at(PtrLvl)){
            markLabelsForPtr(GlobalPtr);
            auto UseLocs = getUseLocations(GlobalPtr);
            buildDefUseGraph(UseLocs, GlobalPtr);
        }
    }
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
std::vector<FlowSensitivePointerAnalysis::DefUseEdgeTupleTy> FlowSensitivePointerAnalysis::
    initializePropagateList(std::set<const PointerTy*> Pointers, size_t PtrLvl){

    std::vector<DefUseEdgeTupleTy> PropagateList{};
    for(auto Ptr: Pointers){
        // An allocated pointer in llvm also represents its allocation location.
        auto Loc = dyn_cast<ProgramLocationTy>(Ptr);
        assert(Loc && "Cannot use nullptr as program location");
        auto InitialDUEdges = getAffectUseLocations(Loc, Ptr);
        for(auto UseLoc : InitialDUEdges){
            PropagateList.push_back(std::make_tuple(Loc, UseLoc, Ptr));
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

    // bug: For a store instruction store x y, if we pass pts(y) from DefLoc to
    //      UseLoc, it will overwrites existing pts(y) at UseLoc. Furthermore, we 
    //      do not really need to pass pts(y) to a store instruction since we are
    //      checking the alias set to perform which kind of points-to set
    //      update. Will remove the use label at store instruction in the future.
    if(!dyn_cast<StoreInst>(UseLoc)){
        PointsToSet[UseLoc][Ptr].insert(PointsToSet.at(DefLoc).at(Ptr).begin(), PointsToSet.at(DefLoc).at(Ptr).end());
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
    if(dyn_cast<Argument>(ValueOperand)){
        if(FuncParas2AliasSet.count(Loc->getFunction()) && FuncParas2AliasSet[Loc->getFunction()].count(ValueOperand)){
            Pointees =  FuncParas2AliasSet.at(Loc->getFunction()).at(ValueOperand);
        }
    }
    else{
        Pointees.insert(ValueOperand->stripPointerCasts());
        if(AliasMap.count(Loc) && AliasMap[Loc].count(ValueOperand)){
            Pointees = AliasMap.at(Loc).at(ValueOperand);
        }
    }
    
    return Pointees;
}

/// @brief Update points-to-set for \p Ptr at program location \p Loc.
/// @return True if the points-to set is changed.
bool FlowSensitivePointerAnalysis::updatePointsToSetAtProgramLocation(const ProgramLocationTy *Loc, 
    const PointerTy *Ptr, std::set<const PointerTy*> PTS){
    auto OldPTS = std::set<const Value*>{};
    if(PointsToSet.count(Loc) && PointsToSet[Loc].count(Ptr)){
        OldPTS = PointsToSet.at(Loc).at(Ptr);
    }
    
    if(OldPTS != PTS){
        PointsToSet[Loc][Ptr] = PTS;
        return true;
    }
    return false;
}

/// @brief Perform either strong update or weak update for \p Pointer at \p Loc
///     according to the size of aliases of \p Pointer. Add def-use edges to
///     \p PropagateList if needed.
void FlowSensitivePointerAnalysis::updatePointsToSet(const ProgramLocationTy *Loc,
     const PointerTy *Pointer, std::set<const PointerTy *> AdjustedPointsToSet, 
     std::vector<DefUseEdgeTupleTy> &PropagateList){

    assert(Pointer && "Cannot update PTS for nullptr");

    auto AliasSet = std::set<const PointerTy *>{};
    if(AliasMap.count(Loc) && AliasMap[Loc].count(Pointer)){
        AliasSet = AliasMap.at(Loc).at(Pointer);
    }
    
    
    
    bool Changed = false;
    if(AliasSet.size() <= 1){
        // Strong update
        Changed = updatePointsToSetAtProgramLocation(Loc, Pointer, AdjustedPointsToSet);
    }
    else{
        // Weak update

        for(auto Alias : AliasSet){
            if(dyn_cast<LoadInst>(Alias)){
                continue;
            }
            auto PTS = PointsToSet[Loc][Alias];
            PTS.insert(AdjustedPointsToSet.begin(), AdjustedPointsToSet.end());
            auto Defs = findDefFromPrevOfUseLoc(Loc, Alias);
            for(auto Def : Defs){
                PTS.insert(PointsToSet[Def][Alias].begin(), PointsToSet[Def][Alias].end());
            }

            if(updatePointsToSetAtProgramLocation(Loc, Alias, PTS)){
                for(auto UseLoc : getAffectUseLocations(Loc, Alias)){    
                    PropagateList.push_back(std::make_tuple(Loc, UseLoc, Alias));
                }
            }
        }
    }

    if(Changed){
        for(auto UseLoc : getAffectUseLocations(Loc, Pointer)){    
            PropagateList.push_back(std::make_tuple(Loc, UseLoc, Pointer));
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
        assert(Alias && "Cannot process nullptr");

        // if we have x = load y, and y alias z, we will need pts(z) at current program location.
        if(!AliasUser.count(Alias)){
            for(auto User : Alias->users()){
                AliasUser[Alias].insert(User);
            }
        }
        AliasUser[Alias].insert(Loc);
        AliasMap[Loc][Load].insert(PointsToSet.at(Loc).at(Alias).begin(), PointsToSet.at(Loc).at(Alias).end());
    }
    return;
}

/// @brief Find all pointers that points to \p Ptr at \p Loc.
std::vector<const FlowSensitivePointerAnalysis::PointerTy*> FlowSensitivePointerAnalysis::
    ptsPointsTo(const ProgramLocationTy *Loc, const PointerTy *Ptr){

    std::vector<const PointerTy*> Res{};

    // if(auto Store = dyn_cast<StoreInst>(Loc)){
    //     Res.push_back(Store->getPointerOperand());
    // }


    // if(PointsToSet.count(Loc)){
    //     for(auto PtsAtPtr : PointsToSet.at(Loc)){
    //         auto it = std::find_if(PtsAtPtr.second.begin(), PtsAtPtr.second.end(), 
    //             [&](const PointerTy *pvar) -> bool {return pvar == Ptr;});
    //         if(it != PtsAtPtr.second.end()){
    //             Res.push_back(PtsAtPtr.first);
    //         }
    //     }
    // }

    if(PointsToSet.count(Loc)){
        for(auto PtsAtPtr : PointsToSet.at(Loc)){
            if(AliasMap[Loc][dyn_cast<StoreInst>(Loc)->getPointerOperand()].count(PtsAtPtr.first) || 
                PtsAtPtr.first == dyn_cast<StoreInst>(Loc)->getPointerOperand()){
                    Res.push_back(PtsAtPtr.first);
                }

        }
    }
    
    return Res;
}

/// @brief Update the alias set for \p ArgIdx-th parameter of function \p Func
///     to \p AliasSet.
void FlowSensitivePointerAnalysis::updateArgAliasOfFunc(const Function* Func, std::set<const Value *> AliasSet, size_t ArgIdx){
    
    const Value *Parameter;
    for(auto &Para : Func->args()){
        Parameter = &Para;
        if(!ArgIdx){
            break;
        }
        --ArgIdx;
    }

    size_t OldSize = 0;
    if(FuncParas2AliasSet.count(Func) && FuncParas2AliasSet[Func].count(Parameter)){
        OldSize = FuncParas2AliasSet.at(Func).at(Parameter).size();
    }
    
    FuncParas2AliasSet[Func][Parameter].insert(AliasSet.begin(), AliasSet.end());

    if(OldSize != FuncParas2AliasSet.at(Func).at(Parameter).size()){
        ReAnalysisFunctions.push_back(Func);
    }
}

/// @brief Propagate the alias set of \p Loc at \p Loc to its use locations.
///     Update its user accordingly.
void FlowSensitivePointerAnalysis::updateAliasUsers(const ProgramLocationTy *Loc, 
    std::vector<DefUseEdgeTupleTy> &PropagateList){
    
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
                    auto PTS = std::set<const PointerTy *>{};
                    if(PointsToSet.count(UseLoc) && PointsToSet[UseLoc].count(Ptr)){
                        PTS = PointsToSet.at(UseLoc).at(Ptr);
                    }
                    if(!Alias){
                        DEBUG_WITH_TYPE("warning", dbgs() << getCurrentTime() << " WARNING: try to update pts for nullptr at"
                            << *UseLoc << " because nullptr is alias to " << *Ptr << "\n");
                            continue;
                    }
                    updatePointsToSet(UseLoc, Alias, PTS, PropagateList);

                    LabelMap[UseLoc].insert(Label(Alias, Label::LabelType::Def));
                    LabelMap[UseLoc].insert(Label(Alias, Label::LabelType::Use));
                    Def2Functions[Alias].insert(UseLoc->getFunction());
                    UseList[Alias].insert(UseLoc);
                    DefList[Alias].insert(UseLoc);

                }
                
            }
            else if(Ptr == Store->getValueOperand()){
                
                auto Pointers = ptsPointsTo(UseLoc, Ptr);
                for(auto Pointer : Pointers){

                    auto OldPTS = std::set<const PointerTy*>{};
                    if(PointsToSet.count(UseLoc) && PointsToSet[UseLoc].count(Pointer)){
                        OldPTS = PointsToSet.at(UseLoc).at(Pointer);
                    }
                                        
                    PointsToSet[UseLoc][Pointer].insert(
                        AliasMap.at(UseLoc).at(Ptr).begin(), 
                        AliasMap.at(UseLoc).at(Ptr).end());


                    if(OldPTS != PointsToSet.at(UseLoc).at(Pointer)){

                        for(auto AffectedLoc : getAffectUseLocations(UseLoc, Pointer)){    
                                PropagateList.push_back(std::make_tuple(UseLoc, AffectedLoc, Pointer));
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
                LabelMap[Load].insert(Label(Alias, Label::LabelType::Use));
                UseList[Alias].insert(Load);
            }
        }
        else if(auto Ret = dyn_cast<ReturnInst>(UseLoc)){
            for(auto Alias : AliasMap.at(UseLoc).at(Ptr)){
                if(dyn_cast<AllocaInst>(Alias) || dyn_cast<LoadInst>(Alias)){
                    LabelMap[UseLoc].insert(Label(Alias, Label::LabelType::Use));
                    UseList[Alias].insert(UseLoc);
                }
            }
        }
        else if (auto Call = dyn_cast<CallInst>(UseLoc)){
            if(!Call->getCalledFunction()){
                // Ignore indirect call.
                continue;
            }

            for(auto Alias : AliasMap.at(UseLoc).at(Ptr)){
                LabelMap[UseLoc].insert(Label(Alias, Label::LabelType::Use));
                UseList[Alias].insert(UseLoc);
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
    
            updateArgAliasOfFunc(Call->getCalledFunction(), AliasMap.at(UseLoc).at(Ptr), ArgumentIdx);
        }
        else{
            DEBUG_WITH_TYPE("fspa", dbgs() << getCurrentTime() << " Cannot process alias user clause type: " 
                << *UseLoc << "\n");

        }
    }
}

/// @brief Update the PTS of \p ArgIdx-th parameter of Func.
void FlowSensitivePointerAnalysis::updateArgPointsToSetOfFunc(const Function *Func, std::set<const Value*> PTS, size_t ArgIdx){
    // Densemap has no at member function in llvm-14. Move back to use std::map.

    const Value *Parameter;
    for(auto &Para : Func->args()){
        Parameter = &Para;
        if(!ArgIdx){
            break;
        }
        --ArgIdx;
    }
    

    auto OldSize = FuncParas2PointsToSet[Func][Parameter].size();
    FuncParas2PointsToSet[Func][Parameter].insert(PTS.begin(), PTS.end());

    if(OldSize != FuncParas2PointsToSet.at(Func).at(Parameter).size()){
        ReAnalysisFunctions.push_back(Func);
    }
}

/// @brief Propagate pointer information along def use graph until fix-point.
void FlowSensitivePointerAnalysis::propagate(std::vector<DefUseEdgeTupleTy> PropagateList, const Function *Func){
   
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
            updatePointsToSet(UseLoc, Store->getPointerOperand(), PTS, PropagateList);


        }
        else if(auto Load = dyn_cast<LoadInst>(UseLoc)){
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
                    for(auto user : UseLoc->users()){
                        AliasUser[UseLoc].insert(user);
                    }
                }
                updateAliasUsers(UseLoc, PropagateList);
            }
        }
        else if(auto Call = dyn_cast<CallInst>(UseLoc)){

            if(!Call->getCalledFunction()){
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

            if(ArgumentIdx < Call->arg_size()){
                updateArgPointsToSetOfFunc(Call->getCalledFunction(), PointsToSet.at(UseLoc).at(Ptr), ArgumentIdx);
            }
            
        }

        PropagateList.erase(PropagateList.begin());
    }
    return;
}

/// @brief Get all functions called by \p Func.
std::set<const Function*> FlowSensitivePointerAnalysis::getCallees(const Function *Func){
    
    if(Caller2Callee.count(Func)){
        return Caller2Callee.at(Func);
    }
    return std::set<const Function *>{};
}

/// @brief Perform pointer analysis for all pointers of pointer level \p PtrLvl 
///     on function \p Func.
void FlowSensitivePointerAnalysis::performPointerAnalysisOnFunction(const Function *Func, size_t PtrLvl){
    DEBUG_WITH_TYPE("fspa", dbgs() << getCurrentTime() << " Analyzing function: " 
        << Func->getName() << " with pointer level: " << PtrLvl << "\n");

    auto Pointers = std::set<const PointerTy*>{};
    if(Func2WorkList.count(Func) && Func2WorkList[Func].count(PtrLvl)){
        Pointers = Func2WorkList.at(Func).at(PtrLvl);
    }
    
    for(auto Ptr : Pointers){
        markLabelsForPtr(Ptr);
        auto UseLocs = getUseLocations(Ptr);
        buildDefUseGraph(UseLocs, Ptr);
    }
    auto PropagateList = initializePropagateList(Pointers, PtrLvl);

    // Also save caller when passing the arguments.
    propagate(PropagateList, Func);
    ++ProcessedFunctionNumber;
}


#ifdef LLVM_TRANSFORM_FLOW_SENSITIVE_POINTER_ANALYSIS_ANALYSIS
    /// @brief Main entry of flow sensitive pointer analysis. Process pointer
    ///        variables level by level. 
    /// @param m 
    /// @param mam 
    /// @return A FlowSensitivePointerAnalysisResult that records points-to 
    ///         set for variables at each program location
    FlowSensitivePointerAnalysisResult FlowSensitivePointerAnalysis::run(Module &m, ModuleAnalysisManager &mam){


        DEBUG_WITH_TYPE("fspa", dbgs() << getCurrentTime() << " Start analyzing module " 
            << m.getName() << "\n");

        auto CurrentPointerLevel = globalInitialize(m);

        auto &FAM = mam.getResult<FunctionAnalysisManagerModuleProxy>(m).getManager();
        
        while(CurrentPointerLevel){
            for(auto &Func : m.functions()){
                // Func2DT.emplace(&Func, std::make_unique<DominatorTreeAnalysis::Result>(&FAM.getResult<DominatorTreeAnalysis>(Func)));
                // Func2DF.emplace(&Func, std::make_unique<DominanceFrontierAnalysis::Result>(&FAM.getResult<DominanceFrontierAnalysis>(Func)));
                processGlobalVariables(CurrentPointerLevel);
                performPointerAnalysisOnFunction(&Func, CurrentPointerLevel);
            }
            while(!ReAnalysisFunctions.empty()){
                auto Func = ReAnalysisFunctions.front();
                performPointerAnalysisOnFunction(Func, CurrentPointerLevel);
                ReAnalysisFunctions.erase(ReAnalysisFunctions.begin());
            }
            --CurrentPointerLevel;

        }

        DEBUG_WITH_TYPE("label", dumpLabelMap());
        DEBUG_WITH_TYPE("pts", dumpPointsToSet());
        DEBUG_WITH_TYPE("pts", dbgs() << getCurrentTime() << " Populating PTS\n");

        populatePointsToSet(m);

        DEBUG_WITH_TYPE("pts", dumpPointsToSet());
        dumpAliasMap();

        AnalysisResult.setFunc2Pointers(Func2AllocatedPointersAndParameterAliases);
        AnalysisResult.setPointsToSet(PointsToSet);

        return AnalysisResult;
    }
#else
    PreservedAnalyses FlowSensitivePointerAnalysis::run(Module &m, ModuleAnalysisManager &mam){
        getCallGraphFromModule(m);

        // todo: Add support for global variables. There should be a global worklist. 

        auto mainFunctionPtr = getFunctionInCallGrpahByName("main");
        if(!mainFunctionPtr){
            outs() << "Cannot find main function.\n";
            return PreservedAnalyses::all();
        } 

        initialize(mainFunctionPtr);
        auto worklist = func2worklist[mainFunctionPtr];
        auto ptrLvl = worklist.size();
        performPointerAnalysisOnFunction(mainFunctionPtr, ptrLvl);

        result.setPointsToSet(pointsToSet);
        return PreservedAnalyses::all();

    }
#endif



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
            OS << "Def(" << *L.Ptr << ")";
        }
        else if(L.Type == Label::LabelType::Use){
            OS << "Use(" << *L.Ptr << ")";
        }
        else if(L.Type == Label::LabelType::DefUse){
            OS << "DefUse(" << *L.Ptr << ")";
        }
        return OS;
    }

}


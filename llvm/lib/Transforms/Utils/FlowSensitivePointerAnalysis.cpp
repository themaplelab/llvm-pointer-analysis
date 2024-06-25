#include "llvm/Transforms/Utils/FlowSensitivePointerAnalysis.h"

#include "llvm/IR/CFG.h"

#include <algorithm>
#include <memory>
#include <exception>
#include <vector>

using namespace llvm;

AnalysisKey FlowSensitivePointerAnalysis::Key;


#ifdef LLVM_TRANSFORM_FLOW_SENSITIVE_POINTER_ANALYSIS_ANALYSIS
    /// @brief Main entry of flow sensitive pointer analysis. Process pointer
    ///        variables level by level. 
    /// @param m 
    /// @param mam 
    /// @return A FlowSensitivePointerAnalysisResult that records points-to 
    ///         set for variables at each program location
    FlowSensitivePointerAnalysisResult FlowSensitivePointerAnalysis::run(Module &m, ModuleAnalysisManager &mam){
        // getCallGraphFromModule(m);

        auto CurrentPointerLevel = globalInitialize(m);
        assert(CurrentPointerLevel >= 0 && "Cannot have negative pointer level.");

        while(CurrentPointerLevel){
            // logger << "Current pointer level: " << CurrentPointerLevel << "\n";
            for(auto &Func : m.functions()){
                // logger << "Current function: " << Func.getName() << "\n";

                processGlobalVariables(CurrentPointerLevel);
                // logger << 1 << "\n";
                performPointerAnalysisOnFunction(&Func, CurrentPointerLevel);
                // logger << 2 << "\n";
            }
            --CurrentPointerLevel;

        }

        result.setPointsToSet(pointsToSet);

        return result;
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


/*

*/
void FlowSensitivePointerAnalysis::processGlobalVariables(int ptrLvl){
    for(auto globalPtr : globalWorkList[ptrLvl]){
        markLabelsForPtr(globalPtr);
        auto useLocs = getUseLocations(globalPtr);
        buildDefUseGraph(useLocs, globalPtr);
    }
}


/*
    Initialize all functions in current module. Return the largest pointer level among all functions.
*/
size_t FlowSensitivePointerAnalysis::globalInitialize(Module &m){
    size_t ptrLvl = 0;
    for(auto &func : m.functions()){
        visited[&func] = false;
        initialize(&func);
        if(ptrLvl < func2worklist[&func].size()){
            ptrLvl = func2worklist[&func].size();
        }
    }
    result.setWorkList(func2worklist);

    return ptrLvl;
}

void FlowSensitivePointerAnalysis::performPointerAnalysisOnFunction(const Function *func, size_t ptrLvl){
    logger << "Analyzing function: " << func->getName() << " with pointer level: " << ptrLvl << "\n";


    visited[func] = true;
    auto pointers = func2worklist[func][ptrLvl];
    // logger << "1.1" << "\n";
    for(auto ptr : pointers){
        // logger << "Preprocessing " << *ptr << "\n";
        markLabelsForPtr(ptr);
        // logger << "1.1.1" << "\n";
        auto useLocs = getUseLocations(ptr);
        // logger << "1.1.2" << "\n";
        buildDefUseGraph(useLocs, ptr);
        // logger << "1.1.3" << "\n";
    }
    // logger << "1.2" << "\n";
    auto propagateList = initializePropagateList(pointers, ptrLvl);
    // logger << "1.3" << "\n";

    // Also save caller when passing the arguments.
    // logger << "Propagating function: " << func->getName() << "\n";c
    propagate(propagateList, func);
    // logger << "1.4" << "\n";


    for(auto callee : getCallees(func)){
        assert(callee && "callee is nullptr");
        // todo: also check if alias set of para is changed.
        if(!visited[callee]){
            performPointerAnalysisOnFunction(callee, ptrLvl);
        }
        
    }
    // logger << "1.5" << "\n";


}

DenseSet<const Function*> FlowSensitivePointerAnalysis::getCallees(const Function *func){
    return caller2Callee[func];
}

DenseSet<const FlowSensitivePointerAnalysis::ProgramLocationTy*> FlowSensitivePointerAnalysis::getUseLocations(const PointerTy *ptr){
    return useList[ptr];
}

void FlowSensitivePointerAnalysis::markLabelsForPtr(const PointerTy *ptr){

    // logger << "Marking labels for " << *ptr << "\n";


    for(auto user : ptr->users()){
        // logger << "Marking labels at " << *user << "\n";
        auto inst = dyn_cast<Instruction>(user);
        if(auto *storeInst = dyn_cast<StoreInst>(inst)){
            labelMap[inst].insert(Label(dyn_cast<Value>(ptr), Label::LabelType::Def));
            labelMap[inst].insert(Label(dyn_cast<Value>(ptr), Label::LabelType::Use));
            useList[ptr].insert(inst);
        }
        else if(auto *loadInst = dyn_cast<LoadInst>(inst)){
            labelMap[inst].insert(Label(dyn_cast<Value>(ptr), Label::LabelType::Use));
            useList[ptr].insert(inst);
        }
        else if(auto *callInst = dyn_cast<CallInst>(inst)){
            labelMap[inst].insert(Label(dyn_cast<Value>(ptr), Label::LabelType::Use));
            useList[ptr].insert(inst);
        }
        else if(auto *retInst = dyn_cast<ReturnInst>(inst)){
            labelMap[inst].insert(Label(dyn_cast<Value>(ptr), Label::LabelType::Use));
            useList[ptr].insert(inst);
        }
        else if(dyn_cast<GetElementPtrInst>(inst) || dyn_cast<BitCastInst>(inst) || dyn_cast<CmpInst>(inst) ||
                dyn_cast<InvokeInst>(inst) || dyn_cast<VAArgInst>(inst) || dyn_cast<PHINode>(inst) || dyn_cast<PtrToIntInst>(inst)){
            #define DEBUG_TYPE "TESTCALLGRAPH"
            LLVM_DEBUG(dbgs() << *inst << " is in the user list of pointer " << *ptr << ", but it's neither storeinst nor loadinst.\n");
            #undef DEBUG_TYPE
        }
        else{
            std::string str, str0;
            raw_string_ostream(str0) << *inst;
            str = "Cannot process instruction:" + str0 + "\n";
            llvm_unreachable(str.c_str());
        }
        // logger << "Finish " << *user << "\n";

    }
}

void FlowSensitivePointerAnalysis::buildDefUseGraph(DenseSet<const ProgramLocationTy*> useLocs, const PointerTy *ptr){
    for(auto useLoc : useLocs){
        // logger << "Building def-use graph for " << *ptr << " at " << *useLoc << "\n";
        auto Visited = DenseSet<const ProgramLocationTy*>();
        // auto defLocs = FindDefInBasicBlock(useLoc, ptr, visited);
        auto defLocs = FindDefFromUseLoc(useLoc, ptr, Visited);
        // logger << "Find " << defLocs.size() << " defs.\n";
        for(auto def : defLocs){
            addDefUseEdge(def, useLoc, ptr);
        }
        // logger << "Finish add edge\n";
    }
}

std::vector<FlowSensitivePointerAnalysis::DefUseEdgeTupleTy> FlowSensitivePointerAnalysis::initializePropagateList(DenseSet<const PointerTy*> pointers, size_t ptrLvl){
    std::vector<DefUseEdgeTupleTy> propagateList;
    for(auto ptr: pointers){
        auto Loc = dyn_cast<Instruction>(ptr);
        assert(Loc && "cannot use nullptr as program location");
        auto initialDUEdges = getAffectUseLocations(Loc, ptr);
        for(auto pu : initialDUEdges){
            propagateList.push_back(std::make_tuple(Loc, pu, ptr));
            // logger << "Insert def-use edge " << *Loc << " == " << *ptr << " ==> " << *pu << "\n";
        }
    }
    for(auto ptr : globalWorkList[ptrLvl]){

    }

    return propagateList;

}

/*
    Build def-use graph and propagate point-to information for pointers of a specific pointer level.
*/
void FlowSensitivePointerAnalysis::propagate(std::vector<DefUseEdgeTupleTy> propagateList, const Function *Func){

    
    while(!propagateList.empty()){
        // logger.note();
        // logger << "Current propagate list size: " << propagateList.size() << "\n";

        auto tup = propagateList.front();
        auto f = std::get<0>(tup);
        auto t = std::get<1>(tup);
        auto ptr = std::get<2>(tup);

        propagatePointsToInformation(t, f, ptr);

        if(auto storeInst = dyn_cast<StoreInst>(t)){
            auto pts = getRealPointsToSet(t, storeInst->getValueOperand());
            updatePointsToSet(t, ptr, pts, propagateList);
        }
        else if(auto loadInst = dyn_cast<LoadInst>(t)){
            auto tmp = aliasMap[t][t];
            updateAliasInformation(t,loadInst);
            // Everytime we update the alias information for pointer pt at location t, we need to add the program locaton t to the users of pt.
            if(tmp != aliasMap[t][t]){
                if(!aliasUser.count(t)){
                    aliasUser[t] = std::set<const User*>();
                    for(auto user : t->users()){
                        aliasUser[t].insert(user);
                    }
                }
                updateAliasUsers(aliasUser[t], t, propagateList);
            }
        }
        propagateList.erase(propagateList.begin());
    }

    return;

}

void FlowSensitivePointerAnalysis::updateAliasUsers(std::set<const User *> users, const ProgramLocationTy *Loc, std::vector<DefUseEdgeTupleTy> &propagateList){
    
    // logger << "Updating alias users for " << *Loc << "\n";
    for(auto user : users){
        
        // logger << "\tWorking with user " << *user << "\n";
        
        auto userInst = dyn_cast<Instruction>(user);
        aliasMap[userInst][Loc] = aliasMap[Loc][Loc];

        if(auto storeInst = dyn_cast<StoreInst>(userInst)){
            if(Loc == storeInst->getPointerOperand()){
                
                if(auto ptr = dyn_cast<Instruction>(storeInst->getValueOperand())){
                    if(aliasMap[userInst][ptr].empty()){
                        updatePointsToSet(userInst, Loc, DenseSet<const Value*>{ptr}, propagateList);
                        for(auto tt0 : aliasMap[userInst][Loc]){
                            // logger << *Loc << " alias to " << *tt0 << "which is a " << *(tt0->getType()) << "\n";
                            // bug: not all tt0 is an instruction, some are arguments.
                            updatePointsToSet(userInst, tt0, pointsToSet[userInst][Loc].first, propagateList);
                        }
                    }
                    else{
                        pointsToSet[userInst][Loc].first = aliasMap[userInst][storeInst->getValueOperand()];
                        for(auto tt0 : aliasMap[userInst][Loc]){
                            updatePointsToSet(userInst, tt0, pointsToSet[userInst][Loc].first, propagateList);
                        }
                    }
                }
                else{
                    updatePointsToSet(userInst, Loc, DenseSet<const Value*>{storeInst->getValueOperand()}, propagateList);
                }


                for(auto tt : aliasMap[userInst][Loc]){
                    labelMap[userInst].insert(Label(tt, Label::LabelType::Def));
                    labelMap[userInst].insert(Label(tt, Label::LabelType::Use));
                    useList[tt].insert(userInst);

                }
                
            }
            else if(Loc == storeInst->getValueOperand()){
                // logger << "\tFind value operand" << "\n";
                
                // get all pointers that point to loc
                auto ptrChangeList = ptsPointsTo(userInst,Loc);
                
                for(auto p0 : ptrChangeList){
                    assert(p0 && "Cannot process nullptr");
                    // logger << "\t\tChanging the points-to set for pointer " << *p0 << "\n";
                    auto tmp = pointsToSet[userInst][p0].first;
                    auto aliasSet = aliasMap[userInst][Loc];
                    pointsToSet[userInst][p0].first.insert(aliasSet.begin(), aliasSet.end());

                    // Here, if points2set is changed, we need to propagate.
                    if(tmp != pointsToSet[userInst][p0].first){
                        // logger << "updateAliasUser Original points-to set of " << *p0 << " at " << *Loc << "\n";
                        // for(auto p : tmp){
                        //     logger << *p << "\n";
                        // }

                        // logger << "New points-to set of " << *p0 << " at " << *Loc << "\n";
                        // for(auto p : pointsToSet[userInst][p0].first){
                        //     logger << *p << "\n";
                        // }
                        auto passList = getAffectUseLocations(userInst, p0);
                        for(auto u : passList){    
                                propagateList.push_back(std::make_tuple(userInst,u,p0));
                                // logger << "Insert def-use edge " << *userInst << " == " << *p0 << " ==> " << *u << "\n";
                        }
                    }
                }
            }
            else{
                errs() << "Hitting at " << *storeInst << " with pointer " << *Loc << "\n";
            }
        }
        else if(auto pt0 = dyn_cast<LoadInst>(userInst)){

            for(auto tt : aliasMap[userInst][Loc]){
                labelMap[userInst].insert(Label(tt, Label::LabelType::Use));
                useList[tt].insert(userInst);
            }

        }
        else if(auto pt0 = dyn_cast<ReturnInst>(userInst)){

            for(auto tt : aliasMap[userInst][Loc]){
                if(dyn_cast<AllocaInst>(tt) || dyn_cast<LoadInst>(tt)){
                    labelMap[userInst].insert(Label(tt, Label::LabelType::Use));
                    useList[tt].insert(userInst);
                }
                
            }
        }
        else if (auto CallInstruction = dyn_cast<CallInst>(userInst)){
            // Ignore indirect call now.
            if(!CallInstruction->getCalledFunction()){
                continue;
            }

            // Find corresponding para index.
            size_t ArgumentIdx = 0;
            for(auto arg : CallInstruction->operand_values()){
                if(arg == Loc){
                    break;
                }
                ++ArgumentIdx;
            }
            // logger << "Arg is" << *Loc << " at " << *CallInstruction << "\n";


            assert((ArgumentIdx < CallInstruction->arg_size()) && "Cannot find argument index at function.");
            

            auto changed = updateArgAliasOfFunc(CallInstruction->getCalledFunction(), aliasMap[userInst][Loc], ArgumentIdx);
        }
        else{
            #define DEBUG_TYPE "TESTCALLGRAPH"
            LLVM_DEBUG(dbgs() << "Wrong clause type: " << *userInst << "\n");
            #undef DEBUG_TYPE
        }

    }
}

bool FlowSensitivePointerAnalysis::updateArgPtsofFunc(const Function *func, const PointerTy *ptr, DenseSet<const Value*> pts){
    auto oldSize = funcParas2PointsToSet[func][ptr].size();
    funcParas2PointsToSet[func][ptr].insert(pts.begin(), pts.end());

    return (oldSize != funcParas2PointsToSet[func][ptr].size());
}

bool FlowSensitivePointerAnalysis::updateArgAliasOfFunc(const Function* func, DenseSet<const Value *> aliasSet, size_t argIdx){
    const Value *para;
    for(auto &pa : func->args()){
        para = &pa;
        if(!argIdx){
            break;
        }
        --argIdx;
    }
    auto oldSize = funcParas2AliasSet[func][para].size();
    funcParas2AliasSet[func][para].insert(aliasSet.begin(), aliasSet.end());

    return (oldSize != funcParas2AliasSet[func][para].size());
}

/// @brief Perform either strong update or weak update for \p Pointer at \p Loc
///     according to the size of aliases of \p Pointer. Add def-use edges to
///     \p propagateList if needed.
/// @param Loc 
/// @param Pointer 
/// @param AdjustedPointsToSet 
/// @param propagateList 
void FlowSensitivePointerAnalysis::updatePointsToSet(const ProgramLocationTy *Loc, const Value *Pointer, DenseSet<const Value *> AdjustedPointsToSet, std::vector<DefUseEdgeTupleTy> &propagateList){
    auto tmp = aliasMap[Loc][Pointer];
    assert(Pointer && "Cannot process nullptr");
    
    auto originalPts = pointsToSet[Loc][Pointer].first;
    if(tmp.size() <= 1){
        pointsToSet[Loc][Pointer].first = AdjustedPointsToSet;
    }
    else{
        pointsToSet[Loc][Pointer].first.insert(AdjustedPointsToSet.begin(), AdjustedPointsToSet.end());
    }
    // Here, if points2set is changed, we need to propagate.
    if(originalPts != pointsToSet[Loc][Pointer].first){
        // logger << "UpdatePointsToSet Original points-to set of " << *Pointer << " at " << *Loc << "\n";
        // logger << tmp.size() << " pointer alias to " << *Pointer << "\n";
        // for(auto p : originalPts){
        //     logger << *p << "\n";
        // }

        // logger << "New points-to set of " << *Pointer << " at " << *Loc << "\n";
        // for(auto p : pointsToSet[Loc][Pointer].first){
        //     logger << *p << "\n";
        // }

        auto affectedUseLocs = getAffectUseLocations(Loc, Pointer);
        for(auto UseLoc : affectedUseLocs){    
                propagateList.push_back(std::make_tuple(Loc,UseLoc,Pointer));
                // logger << "Insert def-use edge " << *Loc << " == " << *Pointer << " ==> " << *UseLoc << "\n";
        }
    }
    
}

/// @brief Update the alias set of pointer x introduced by a loadInst "x = load y"
/// @param loc 
/// @param loadInst 
void FlowSensitivePointerAnalysis::updateAliasInformation(const ProgramLocationTy *Loc, const LoadInst *loadInst){
    
    auto aliases = getAlias(Loc, loadInst);
    for(auto &b : aliases){
        // logger << "Alias of " << *loadInst << " : " << *b << "\n";
        // bug: b can be either instruction or argument.

        // auto pointer = dyn_cast<Instruction>(b);
        assert(b && "Cannot process nullptr");
        if(!aliasUser.count(b)){
            aliasUser[b] = std::set<const User*>();
            for(auto user0 : b->users()){
                aliasUser[b].insert(user0);
            }
        }
        auto user = dyn_cast<User>(Loc);
        assert(user && "Cannot process nullptr");
        aliasUser[b].insert(user);


        aliasMap[Loc][Loc].insert(pointsToSet[Loc][b].first.begin(), pointsToSet[Loc][b].first.end());
    }
    
    return;
}

DenseSet<const Value*> FlowSensitivePointerAnalysis::getAlias(const ProgramLocationTy *t, const Instruction *p){
    // for a store inst "store a b", we get the alias set of a at t.
    if(auto pt = dyn_cast<StoreInst>(p)){
        auto pointees = aliasMap[t][pt->getValueOperand()];
        if(pointees.empty()){
            return DenseSet<const Value*>{dyn_cast<Instruction>(pt->getValueOperand())};
        }
        return pointees;
    }
    // for a "a = load b", we get the alias set of b at t.
    else if(auto pt = dyn_cast<LoadInst>(p)){
        auto pointees = aliasMap[t][pt->getPointerOperand()];
        if(pointees.empty()){
            return DenseSet<const Value*>{dyn_cast<Instruction>(pt->getPointerOperand())};
        }
        return pointees;
    }
    else{
        std::string str;
        raw_string_ostream(str) << *p;
        str = "Getting alias for non store nor load instruction (" + str + ").\n";
        llvm_unreachable(str.c_str());
    }
}


/// @brief Get the set of real pointee represented by a pointer. For a storeInst 
///     store x y, x maybe an parameter of a function or a temporary register. In our
///     analysis, we only want to propagate allocated pointer. Return itself iff no
///     allocated pointers are alias to it.
/// @param Loc Program location that we want to query the alias set.
/// @param ValueOperand Value operand of a store instruction. We will find all allocated pointers that
///     alias to it.
/// @return  A set of allocated pointers or \p ValueOperand.
DenseSet<const Value*> FlowSensitivePointerAnalysis::getRealPointsToSet(const ProgramLocationTy *Loc, const Value *ValueOperand){
    
    DenseSet<const Value*> pointees;
    if(dyn_cast<Argument>(ValueOperand)){
        pointees =  funcParas2AliasSet[Loc->getFunction()][ValueOperand];
    }
    else{
        pointees = aliasMap[Loc][ValueOperand];
    }
    
    return (pointees.empty() ? DenseSet<const Value*>{ValueOperand->stripPointerCasts()} : pointees);

}

/// @brief 
/// @param loc 
/// @param ptr 
/// @return 
std::vector<const FlowSensitivePointerAnalysis::ProgramLocationTy*> FlowSensitivePointerAnalysis::getAffectUseLocations(const ProgramLocationTy *loc, const Value *ptr){
    std::vector<const Instruction*> res;

    // outs() << "find for " << *ptr << " at " << *loc << "\n";

    for(auto iter = defUseGraph[loc].begin(); iter != defUseGraph[loc].end(); ++iter){
        if(ptr == iter->first){
            res.insert(res.begin(), iter->second.begin(), iter->second.end());
        }
    }
        
    return res;
}

void FlowSensitivePointerAnalysis::propagatePointsToInformation(const ProgramLocationTy *toLoc, const ProgramLocationTy *fromLoc, const PointerTy *var){
    // bug: For a store instruction store x y, if we pass pts(y) from fromLoc to
    //      toLoc, it will overwrites existing pts(y) at toLoc. Furthermore, we 
    //      do not really need to pass pts(y) to a store instruction since we are
    //      checking the alias set to perform which kind of points-to set
    //      update. Will remove the use label at store instruction in the future.
    if(dyn_cast<LoadInst>(toLoc)){
        auto oldPointsToSet = pointsToSet[toLoc][var].first;
        pointsToSet[toLoc][var].first.insert(pointsToSet[fromLoc][var].first.begin(), pointsToSet[fromLoc][var].first.end());
        // if(pointsToSet[toLoc][var].first != oldPointsToSet){
        //     pointsToSet[toLoc][var].second = true;
        // }
    }

    return;
}

void FlowSensitivePointerAnalysis::addDefUseEdge(const ProgramLocationTy *def, const ProgramLocationTy *use, const PointerTy *ptr){
    defUseGraph[def][ptr].insert(use);
}

DenseSet<const Instruction*> FlowSensitivePointerAnalysis::FindDefFromUseLoc(const ProgramLocationTy *Loc, const PointerTy *Ptr, DenseSet<const ProgramLocationTy*> &Visited){

    if(Visited.contains(Loc)){
        return DenseSet<const Instruction*>{};
    }
    Visited.insert(Loc);
    // if(Loc->getFunction()->getName().str() == "walk_tree_1"){
    //     logger << "Finding defs of " << *Ptr << " at program location " << *Loc << "\n";

    // }


    // Be careful not to implicit create new entry when finding existence.
    if(DefLoc[Loc].count(Ptr)){
        return DefLoc[Loc][Ptr];
    }
    else{
        DenseSet<const ProgramLocationTy*> res{};
        if(hasDef(Loc, Ptr)){
            res = DenseSet<const ProgramLocationTy*>{Loc};
        }
        else{
            auto Prev = Loc->getPrevNonDebugInstruction();
            if(Prev){
                res = FindDefFromUseLoc(Prev, Ptr, Visited);
            }
            else{
                // if(Loc->getFunction()->getName().str() == "walk_tree_1"){
                //     logger << "aaa" << "\n";
                // }
                auto PrevBasicBlocksRange = predecessors(Loc->getParent());

                // if(Loc->getFunction()->getName().str() == "walk_tree_1"){
                //     logger << "bbb" << PrevBasicBlocksRange.empty() << "\n";
                // }
                
                if(PrevBasicBlocksRange.empty()){
                    // First basicblock reached. Search caller function next.
                    if(!func2CallerLocation.count(Loc->getFunction())){
                        return res;
                    }
                    for(auto callLoc : func2CallerLocation[Loc->getParent()->getParent()]){
                        auto defs = FindDefFromUseLoc(callLoc, Ptr, Visited);
                        res.insert(defs.begin(), defs.end());
                    }

                }
                else{
                    for(auto *PrevBasicBlock : PrevBasicBlocksRange){
                        Prev = &(PrevBasicBlock->back());
                        auto defs = FindDefFromUseLoc(Prev, Ptr, Visited);
                        res.insert(defs.begin(), defs.end());
                    }
                }
            }
        }

        DefLoc[Loc][Ptr] = res;
        return res;
    }
}




/// @brief Find all definition locations in current basicblock starting from a program location. 
///        LLVM has some intrinsic functions for mapping between LLVM program objects and the source-level objects. 
///        These debug instructions are not related to our analysis.
/// @param loc Program location that use \p ptr
/// @param ptr pointer that being used
/// @return A set of definition locations
DenseSet<const Instruction*> FlowSensitivePointerAnalysis::FindDefInBasicBlock(const ProgramLocationTy *loc, const PointerTy *ptr, DenseSet<const BasicBlock*> &visited){

    if(visited.count(loc->getParent())){
        return DenseSet<const Instruction*>();
    }

    // logger << "Finding defs of " << *ptr << " at program location " << *loc << "\n";
    visited.insert(loc->getParent());

    if(auto callInst = dyn_cast<CallInst>(loc)){
        DenseSet<const llvm::Instruction *> defLocs;
        if(callInst->getCalledFunction()){
            defLocs = findDefFromFunc(callInst->getCalledFunction(), ptr, visited);
        }
        return defLocs;
    }

    DenseSet<const ProgramLocationTy*> res;
    while(true){
        auto prevLoc = loc->getPrevNonDebugInstruction();
        if(prevLoc){
            if(auto callInst = dyn_cast<CallInst>(prevLoc)){
                DenseSet<const llvm::Instruction *> defLocs;
                if(callInst->getCalledFunction()){
                    defLocs = findDefFromFunc(callInst->getCalledFunction(), ptr, visited);
                }
                return defLocs;
            }
            if(hasDef(prevLoc, ptr)){
                return DenseSet<const Instruction*>{prevLoc};
            }
            loc = prevLoc;
        }
        else{
            DenseSet<const ProgramLocationTy*> res;
            for(auto it = pred_begin(loc->getParent()); it != pred_end(loc->getParent()); ++it){
                auto defs = findDefFromBB(*it, ptr, visited);
                res.insert(defs.begin(), defs.end());
            }
            
            return res;
        }
    }
}

/// @brief Find all definition locations for ptr within a function func.
/// @param func 
/// @param ptr 
/// @return 
DenseSet<const Instruction*> FlowSensitivePointerAnalysis::findDefFromFunc(const Function *func, const PointerTy *ptr, DenseSet<const BasicBlock*> &visited){
    // logger << "Finding defs of " << *ptr << " in function " << func->getName() << "\n";
    DenseSet<const ProgramLocationTy*> res;

    // bug: if func is not an entry if func2TerminateBBs, 
    auto terminatedBBs = func2TerminateBBs[func];
    for(auto it = terminatedBBs.begin(), end = terminatedBBs.end(); it != end; ++it){
        auto bb = *it;
        assert(bb && "Cannot handle nullptr");
        // logger << "a.1\n";
        auto defs = findDefFromBB(bb, ptr, visited);
        // logger << "a.2\n";
        res.insert(defs.begin(), defs.end());
        // logger << "a.3\n";
    }

    // logger << "a.4\n";
    return res;
}

DenseSet<const Instruction*> FlowSensitivePointerAnalysis::findDefFromBB(const BasicBlock *bb, const PointerTy *p, DenseSet<const BasicBlock*> &visited){
    
    // logger << "Finding defs of " << *p << " in basic block " << bb->getName() << "\n";
    if(visited.count(bb)){
        return DenseSet<const Instruction*>();
    }
    visited.insert(bb);
    
    auto lastInst = &(bb->back());

    while(lastInst){
        if(auto callInst = dyn_cast<CallInst>(lastInst)){
            DenseSet<const llvm::Instruction *> defLocs;
            if(callInst->getCalledFunction()){
                defLocs = findDefFromFunc(callInst->getCalledFunction(), p, visited);
            }
            return defLocs;
        }

        if(hasDef(lastInst, p)){
            return DenseSet<const Instruction*>{lastInst};
        }
        lastInst = lastInst->getPrevNonDebugInstruction();
    }
    DenseSet<const Instruction*> res;
    
    for(auto it = pred_begin(bb); it != pred_end(bb); ++it){
        if(visited.find(*it) == visited.end()){
            DenseSet<const Instruction*> defs = findDefFromBB(*it, p, visited);
            res.insert(defs.begin(), defs.end());
        }

    }

    if(res.empty()){
        for(auto callLoc : func2CallerLocation[bb->getParent()]){
            auto defs = FindDefInBasicBlock(callLoc, p, visited);
            res.insert(defs.begin(), defs.end());
        }
    }


    return res;
}

bool FlowSensitivePointerAnalysis::hasDef(const ProgramLocationTy *loc, const PointerTy *ptr){
    // outs() << "HasDef At" << *loc << " with ptr" << *ptr << "\n";
    auto iter = std::find_if(labelMap[loc].begin(), labelMap[loc].end(), [&](Label l) -> bool {
        return l.type == Label::LabelType::Def && l.ptr == ptr;
        });
    // outs() << "End\n";
    return (iter == labelMap[loc].end() ? false : true);
}

size_t FlowSensitivePointerAnalysis::computePointerLevel(const Instruction *inst){
    size_t pointerLevel = 1;

    auto ty = inst->getType();
    while(ty->getPointerElementType()->isPointerTy()){
        ++pointerLevel;
        ty = ty->getPointerElementType();
    }
    return pointerLevel;
}

void FlowSensitivePointerAnalysis::initialize(const Function * const func){
    /*
       Calculate pointer level for each pointer in func

    */

   /*
    Since two pointers used in load or store different in exact 1 points-to level, we do not need to store the 1.
    A pair (a,b) means a + 1 = b
    
   */

    // logger.note();
    // logger << "Initializing " + func->getName() + "\n";

    // Do not initialize a function twice.
    if(func2worklist.count(func)){
        return;
    }

    llvm::DenseMap<size_t, llvm::DenseSet<const PointerTy *>> worklist;


    for(auto &inst : instructions(*func)){

        if(const AllocaInst *alloca = dyn_cast<AllocaInst>(&inst)){
            auto PointerLevel = computePointerLevel(&inst);
            worklist[PointerLevel].insert(&inst);
            labelMap[alloca].insert(Label(&inst, Label::LabelType::Def));
            // Empty points-to set means the pointer is undefined.
            pointsToSet[&inst][&inst] = {DenseSet<const Value*>(), false};
        }
        else if(const CallInst *callInst = dyn_cast<CallInst>(&inst)){
            func2CallerLocation[callInst->getCalledFunction()].insert(&inst);
            if(!callInst->getCalledFunction()){
                // todo: move the comment back into code.

                // logger.warning();
                // logger << *callInst << " performs an indirect call\n";
            }
            else{
                caller2Callee[func].insert(callInst->getCalledFunction());
            }
            
        }
        else if(const ReturnInst *retInst = dyn_cast<ReturnInst>(&inst)){
            assert(retInst->getParent() && "Cannot insert nullptr");
            func2TerminateBBs[func].insert(retInst->getParent());
        }
    }

    // logger << "Worklist for " << func->getName() << "\n";
    // for(auto p : worklist){
    //     logger << p.first << "\n";
    //     for(auto e : p.second){
    //         logger << *e << " ";
    //     }
    //     logger << "\n";
    // }


    func2worklist[func] = worklist;
    
    return;
}

// const Function* FlowSensitivePointerAnalysis::getFunctionInCallGrpahByName(std::string name){
//     using FunctionMapValueType = std::pair<const Function *const, std::unique_ptr<CallGraphNode>>;

//     /*
//         We have to test if the first part is nullptr when traversing the callgraph. The callgraph uses nullptr to represent an
//         ExtrenalCallingNode or a CallsExternalNode. For example, for main function, dumping the callgraph will show "CS<None> 
//         calls function 'main'"
//     */
//     auto res = std::find_if(cg->begin(), cg->end(), [name](FunctionMapValueType &p) -> bool {
//         return p.first && p.first->getName() == name;
//         });

//     return (res == cg->end() ? nullptr : res->first);
// }







void FlowSensitivePointerAnalysis::dumpWorkList(){
    // DenseMap<size_t, DenseSet<const Instruction*>> worklist;

    // for(auto beg = func2worklist.begin(), end = func2worklist.end(); beg != end; ++beg){
    //     size_t ptrLevel = beg->first;
    //     errs() << "Pointer level: " << ptrLevel << ":\n";
    //     DenseSet<const Instruction*> elements = beg->second;
    //     for(auto b = elements.begin(), e = elements.end(); b != e; ++b){
    //         errs() << *(*b) << "\n";
    //     }
    // }

}

void FlowSensitivePointerAnalysis::dumpLabelMap(){
    errs() << "Dump Labelmap.\n";
    for(auto beg = labelMap.begin(), end = labelMap.end(); beg != end; ++beg){
        errs() << *(beg->first) << "\n";
        for(auto b = beg->second.begin(), e = beg->second.end(); b != e; ++b){
            errs() << "\t" << *b << "\n";
        }
    }
}

void FlowSensitivePointerAnalysis::dumpPointsToMap(){
    errs() << "Dump points-to map.\n";
    for(auto beg = pointsToSet.begin(), end = pointsToSet.end(); beg != end; ++beg){
        auto inst = beg->first;
        errs() << "\tAt program location: " << *inst << ":\n";
        for(auto b = beg->second.begin(), e = beg->second.end(); b != e; ++b){
            auto ptr = b->first;
            auto pointees = b->second;
            errs() << "\t" << *ptr << " ==>\n";
            if(pointees.first.empty()){
                errs() << "\t\tundefined\n";
            }
            else{
                for(auto pointee : pointees.first){
                    if(pointee){
                        errs() << "\t\t" << *pointee << "\n";
                    }
                    else{
                        errs() << "\t\tinvalid pointer" << "\n";
                    }
            }
            }
            
        }
        for(auto b0 = aliasMap[inst].begin(), e0 = aliasMap[inst].end(); b0 != e0; ++b0){
            for(auto a : b0->second){
                if(a){
                    errs() << "\t" << *b0->first << " is alias to " << *a << "\n";
                }
            }
        }
    }

}


void FlowSensitivePointerAnalysis::dumpDefUseGraph() const{
    // errs() << "Dump Def-use graph.\n";
    // #define DEBUG_STR_LENGTH 30
    // errs() << "\n" << std::string(DEBUG_STR_LENGTH, '*') << RED_BOLD_PREFIX "def use graph" ALL_RESET << std::string(DEBUG_STR_LENGTH, '*') << "\n";
    // for(auto it = defUseGraph.begin(); it != defUseGraph.end(); ++it){
    //     for(auto iter = it->second.begin(), end = it->second.end(); iter != end; ++iter){
    //         auto ptr = iter->first;
    //         for(auto i = iter->second.begin(), e = iter->second.end(); i != e; ++i){
    //             errs() << YELLOW_BOLD_PREFIX << *(it->first) << ALL_RESET " === " << **i << " ===> " << *ptr << "\n";
    //         }
    //     }
    //     // for(auto ptr : it->second->second){
    //     //     errs() << YELLOW_BOLD_PREFIX << *(it->first) << ALL_RESET " === ";
    //     // }
    //     // ====> " GREEN_BOLD_PREFIX << ALL_RESET << "\n";
    // }
    // errs() << std::string(DEBUG_STR_LENGTH * 2, '*') << "\n\n";
    // #undef DEBUG_STR_LENGTH
}

// bool FlowSensitivePointerAnalysis::notVisited(const Function *f){

// }

// std::vector<const Function*> FlowSensitivePointerAnalysis::collectAllCallees(const Function *f){
//     auto node = (*cg)[f];
// }



std::vector<const Value*> FlowSensitivePointerAnalysis::ptsPointsTo(const Instruction *user, const Instruction *t){
    std::vector<const Value*> res;

    auto candidatePointers = pointsToSet[user];
    for(auto iter = candidatePointers.begin(); iter != candidatePointers.end(); ++iter){
        auto pts = iter->second;
        auto it = std::find_if(pts.first.begin(), pts.first.end(), [&](const Value *pvar) -> bool {return pvar == t;});
        if(it != pts.first.end()){
            assert(iter->first && "Cannot process nullptr");
            res.push_back(iter->first);
        }
    }

    return res;
}



namespace llvm{
    bool operator<(const Label &l1, const Label &l2){
        if(l1.type == l2.type){
            return l1.ptr < l2.ptr;
        }
        else{
            return l1.type < l2.type;
        }
    }

    raw_ostream& operator<<(raw_ostream &os, const Label &l){
        if(l.type == Label::LabelType::None){
            os << "None";
        }
        else if(l.type == Label::LabelType::Def){
            os << "Def(" << *l.ptr << ")";
        }
        else if(l.type == Label::LabelType::Use){
            os << "Use(" << *l.ptr << ")";
        }
        else if(l.type == Label::LabelType::DefUse){
            os << "DefUse(" << *l.ptr << ")";
        }
        return os;
    }

    raw_ostream& operator<<(raw_ostream &os, const std::map<const Instruction*, std::map<const Instruction*, std::pair<std::set<const Value*>, bool>>> &pts){
        os << "pointsToSet is:\n ";
        for(auto pair : pts){
            os << "At " << *pair.first << " " << pair.first <<": \n";
            for(auto p : pair.second){
                for(auto e : p.second.first){
                    os << "\t" << *p.first << " => " << *e << "\n";
                }
            }
        }
        return os;
    }

    raw_ostream& operator<<(raw_ostream &os, const DenseMap<size_t, DenseSet<const Instruction *>> &wl){
        os << "WorkList:\n";
        for(auto pair : wl){
            for(auto e : pair.second){
                os << pair.first << " => " << *e << "\n";
            }
        }

        return os;
    }

    raw_ostream& operator<<(raw_ostream &os, const std::vector<const Instruction *> &l){
        os << "Initial edges: \n";
        for(auto e : l){
            os << *e << "\n";
        }
        return os;
    }

    raw_ostream& operator<<(raw_ostream &os, const std::map<const Instruction*, std::map<const Instruction*, std::set<const Instruction*>>> &l){
        os << "Def Use Graph: \n";
        for(auto it = l.begin(); it != l.end(); ++it){
            for(auto iter = it->second.begin(), end = it->second.end(); iter != end; ++iter){
                auto ptr = iter->first;
                for(auto i = iter->second.begin(), e = iter->second.end(); i != e; ++i){
                    errs() << *(it->first) << " === " << **i << " ===> " << *ptr << "\n";
                }
            }
        }   
        return os;
    }



}


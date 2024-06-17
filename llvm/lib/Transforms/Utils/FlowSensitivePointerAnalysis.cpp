#include "llvm/Transforms/Utils/FlowSensitivePointerAnalysis.h"

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

        while(CurrentPointerLevel){
            for(auto &Func : m.functions()){
                processGlobalVariables(CurrentPointerLevel);
                performPointerAnalysisOnFunction(&Func, CurrentPointerLevel);
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
        initialize(&func);
        if(ptrLvl < func2worklist[&func].size()){
            ptrLvl = func2worklist[&func].size();
        }
    }
    result.setWorkList(func2worklist);

    return ptrLvl;
}

void FlowSensitivePointerAnalysis::performPointerAnalysisOnFunction(const Function *func, size_t ptrLvl){


    visited[func] = true;
    auto pointers = func2worklist[func][ptrLvl];
    for(auto ptr : pointers){
        markLabelsForPtr(ptr);
        auto useLocs = getUseLocations(ptr);
        buildDefUseGraph(useLocs, ptr);
    }
    auto propagateList = initializePropagateList(pointers, ptrLvl);
    // Also save caller when passing the arguments.
    propagate(propagateList, funcParas2AliasSet[func], funcParas2PointsToSet[func]);

    for(auto callee : getCallees(func)){
        performPointerAnalysisOnFunction(callee, ptrLvl);
    }

}

DenseSet<const Function*> FlowSensitivePointerAnalysis::getCallees(const Function *func){
    return caller2Callee[func];
}

DenseSet<const FlowSensitivePointerAnalysis::ProgramLocationTy*> FlowSensitivePointerAnalysis::getUseLocations(const PointerTy *ptr){
    return useList[ptr];
}

DenseSet<const FlowSensitivePointerAnalysis::ProgramLocationTy*> FlowSensitivePointerAnalysis::markLabelsForPtr(const PointerTy *ptr){


    for(auto user : ptr->users()){
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
                dyn_cast<InvokeInst>(inst)){
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
    }
}

void FlowSensitivePointerAnalysis::buildDefUseGraph(DenseSet<const ProgramLocationTy*> useLocs, const PointerTy *ptr){
    for(auto useLoc : useLocs){
        auto defLocs = FindDefInBasicBlock(useLoc, ptr);

        for(auto def : defLocs){
            addDefUseEdge(def, useLoc, ptr);
        }
    }
}

std::vector<FlowSensitivePointerAnalysis::DefUseEdgeTupleTy> FlowSensitivePointerAnalysis::initializePropagateList(DenseSet<const PointerTy*> pointers, size_t ptrLvl){
    std::vector<DefUseEdgeTupleTy> propagateList;
    for(auto ptr: pointers){
        auto initialDUEdges = getAffectUseLocations(ptr, ptr);
        for(auto pu : initialDUEdges){
            propagateList.push_back(std::make_tuple(ptr, pu, ptr));
        }
    }
    for(auto ptr : globalWorkList[ptrLvl]){

    }

}

/*
    Build def-use graph and propagate point-to information for pointers of a specific pointer level.
*/
void FlowSensitivePointerAnalysis::propagate(std::vector<DefUseEdgeTupleTy> propagateList, DenseMap<const Value*, DenseSet<const Value*>> para2Alias, DenseMap<const Value*, DenseSet<const Value*>> para2Pts){

    while(!propagateList.empty()){

        auto tup = propagateList.front();
        auto f = std::get<0>(tup);
        auto t = std::get<1>(tup);
        auto ptr = std::get<2>(tup);

        propagatePointsToInformation(t, f, ptr);

        if(auto storeInst = dyn_cast<StoreInst>(t)){
            auto pts = calculatePointsToInformationForStoreInst(t, storeInst, para2Alias);
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

void FlowSensitivePointerAnalysis::updateAliasUsers(std::set<const User *> users, const ProgramLocationTy *t, std::vector<DefUseEdgeTupleTy> &propagateList){
    for(auto user : users){
        
        auto userInst = dyn_cast<Instruction>(user);
        aliasMap[userInst][t] = aliasMap[t][t];

        if(auto storeInst = dyn_cast<StoreInst>(userInst)){
            if(t == storeInst->getPointerOperand()){
                

                if(auto ptr = dyn_cast<Instruction>(storeInst->getValueOperand())){
                    if(aliasMap[userInst][ptr].empty()){
                        updatePointsToSet(userInst, t, DenseSet<const Value*>{ptr}, propagateList);
                        for(auto tt0 : aliasMap[userInst][t]){
                            auto tt = dyn_cast<Instruction>(tt0);
                            updatePointsToSet(userInst, tt, pointsToSet[userInst][t].first, propagateList);
                        }
                    }
                    else{
                        pointsToSet[userInst][t].first = aliasMap[userInst][storeInst->getValueOperand()];
                        for(auto tt0 : aliasMap[userInst][t]){
                            auto tt = dyn_cast<Instruction>(tt0);
                            updatePointsToSet(userInst, tt, pointsToSet[userInst][t].first, propagateList);
                        }
                    }
                }
                else{
                    updatePointsToSet(userInst, t, DenseSet<const Value*>{storeInst->getValueOperand()}, propagateList);
                }


                for(auto tt : aliasMap[userInst][t]){
                    labelMap[userInst].insert(Label(tt, Label::LabelType::Def));
                    labelMap[userInst].insert(Label(tt, Label::LabelType::Use));
                    useList[tt].insert(userInst);

                }
                
            }
            else if(t == storeInst->getValueOperand()){

                auto ptrChangeList = ptsPointsTo(userInst,t);
                for(auto p0 : ptrChangeList){
                    auto p1 = dyn_cast<Instruction>(p0);
                    pointsToSet[userInst][p1].first = aliasMap[userInst][t];

                    // Here, if points2set is changed, we need to propagate.

                    auto tmp = pointsToSet[userInst][p1];
                    // outs() << "points-to set changed.\n";
                    auto passList = getAffectUseLocations(userInst, p1);
                    for(auto u : passList){    
                            propagateList.push_back(std::make_tuple(userInst,u,p1));
                            // outs() << "New def use edge added to propagatelist: " << *userInst << "=== " << *p1 << " ===>" << *u << "\n";      

                    }
                }
            }
            else{
                errs() << "Hitting at " << *storeInst << " with pointer " << *t << "\n";
            }
        }
        else if(auto pt0 = dyn_cast<LoadInst>(userInst)){

            for(auto tt : aliasMap[userInst][t]){
                labelMap[userInst].insert(Label(tt, Label::LabelType::Use));
                useList[tt].insert(userInst);
            }

        }
        else if(auto pt0 = dyn_cast<ReturnInst>(userInst)){

            for(auto tt : aliasMap[userInst][t]){
                if(dyn_cast<AllocaInst>(tt) || dyn_cast<LoadInst>(tt)){
                    labelMap[userInst].insert(Label(tt, Label::LabelType::Use));
                    useList[tt].insert(userInst);
                }
                
            }
        }
        else if (auto pt0 = dyn_cast<CallInst>(userInst)){

            // Find corresponding para index.
            size_t argIdx = 0;
            for(auto arg : pt0->operand_values()){
                if(arg == t){
                    break;
                }
                ++argIdx;
            }
            auto changed = updateArgAliasOfFunc(pt0->getFunction(), aliasMap[userInst][t], argIdx);
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

void FlowSensitivePointerAnalysis::updatePointsToSet(const ProgramLocationTy *loc, const PointerTy *ptr, DenseSet<const Value *> pts, std::vector<DefUseEdgeTupleTy> &propagateList){
    auto tmp = aliasMap[loc][ptr];
    if(tmp.size() <= 1){
        pointsToSet[loc][ptr].first = pts;
    }
    else{
        pointsToSet[loc][ptr].first.insert(pts.begin(), pts.end());
    }
    // Here, if points2set is changed, we need to propagate.
    if(tmp != pointsToSet[loc][ptr].first){

        pointsToSet[loc][ptr].second = true;
        auto affectedUseLocs = getAffectUseLocations(loc, ptr);
        for(auto u : affectedUseLocs){    
                propagateList.push_back(std::make_tuple(loc,u,ptr));
                #define DEBUG_TYPE "TESTCALLGRAPH"
                LLVM_DEBUG(dbgs() << "New def use edge added to propagatelist: " << *loc << "=== " << *ptr << " ===>" << *u << "\n");
                #undef DEBUG_TYPE
        }
    }
    
}

void FlowSensitivePointerAnalysis::updateAliasInformation(const ProgramLocationTy *loc, const LoadInst *loadInst){
    auto aliases = getAlias(loc, loadInst);

    for(auto &b : aliases){
        auto pointer = dyn_cast<Instruction>(b);
        if(!aliasUser.count(pointer)){
            aliasUser[pointer] = std::set<const User*>();
            for(auto user0 : pointer->users()){
                aliasUser[pointer].insert(user0);
            }
        }
        auto user = dyn_cast<User>(loc);
        aliasUser[pointer].insert(user);


        auto a = dyn_cast<Instruction>(b);
        aliasMap[loc][loc].insert(pointsToSet[loc][a].first.begin(), pointsToSet[loc][a].first.end());
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

DenseSet<const Value*> FlowSensitivePointerAnalysis::calculatePointsToInformationForStoreInst(const ProgramLocationTy *t, const StoreInst *pt, DenseMap<const Value*, DenseSet<const Value*>> para2Alias){
    if(dyn_cast<Argument>(pt->getValueOperand())){
        return para2Alias[pt->getValueOperand()];
    }
    DenseSet<const Value*> pointees = aliasMap[t][pt->getValueOperand()];
    return (pointees.empty() ? DenseSet<const Value*>{pt->getValueOperand()->stripPointerCasts()} : pointees);

}

std::vector<const FlowSensitivePointerAnalysis::ProgramLocationTy*> FlowSensitivePointerAnalysis::getAffectUseLocations(const ProgramLocationTy *loc, const PointerTy *ptr){
    std::vector<const Instruction*> res;

    // outs() << "find for " << *ptr << " at " << *loc << "\n";

    for(auto iter = defUseGraph[loc].begin(); iter != defUseGraph[loc].end(); ++iter){
        if(ptr == iter->first){
            res.insert(res.begin(), iter->second.begin(), iter->second.end());
        }
        // auto s = iter->second;
        // auto it = std::find_if(s.begin(), s.end(), [ptr](const PointerTy *p) -> bool{
        //     outs() << "Comparing " << *p << " with " << *ptr << "\n";
        //     return p == ptr;
        // });
        // if(it != s.end()){
        //     res.push_back(iter->first);
        // }

    }

    // for(auto iter = defUseGraph[loc].begin(); iter != defUseGraph[loc].end(); ++iter){
    //     for(auto it = iter->second.begin(); it != iter->second.end(); ++it){
    //         if(*it == ptr){
    //             res.push_back(iter->first);
    //         }
    //     }
    // }
        
    return res;
}

void FlowSensitivePointerAnalysis::propagatePointsToInformation(const ProgramLocationTy *toLoc, const ProgramLocationTy *fromLoc, const PointerTy *var){
    auto oldPointsToSet = pointsToSet[toLoc][var].first;
    pointsToSet[toLoc][var].first.insert(pointsToSet[fromLoc][var].first.begin(), pointsToSet[fromLoc][var].first.end());
    if(pointsToSet[toLoc][var].first != oldPointsToSet){
        pointsToSet[toLoc][var].second = true;
    }
    return;
}

void FlowSensitivePointerAnalysis::addDefUseEdge(const ProgramLocationTy *def, const ProgramLocationTy *use, const PointerTy *ptr){
    defUseGraph[def][ptr].insert(use);
}

/// @brief Find all definition locations in current basicblock starting from a program location. 
///        LLVM has some intrinsic functions for mapping between LLVM program objects and the source-level objects. 
///        These debug instructions are not related to our analysis.
/// @param loc Program location that use \p ptr
/// @param ptr pointer that being used
/// @return A set of definition locations
DenseSet<const Instruction*> FlowSensitivePointerAnalysis::FindDefInBasicBlock(const ProgramLocationTy *loc, const PointerTy *ptr){

    if(auto callInst = dyn_cast<CallInst>(loc)){
        auto defLocs = findDefFromFunc(callInst->getCalledFunction(), ptr);
        return defLocs;
    }

    while(true){
        auto prevLoc = loc->getPrevNonDebugInstruction();
        if(prevLoc){
            if(auto callInst = dyn_cast<CallInst>(prevLoc)){
                auto defLocs = findDefFromFunc(callInst->getCalledFunction(), ptr);
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
                DenseSet<const BasicBlock*> visited{loc->getParent()};
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
DenseSet<const Instruction*> FlowSensitivePointerAnalysis::findDefFromFunc(const Function *func, const PointerTy *ptr){
    DenseSet<const ProgramLocationTy*> res;
    for(auto bb : func2TerminateBBs[func]){
        auto defs = findDefFromBB(bb, ptr, DenseSet<const BasicBlock*>{bb});
        res.insert(defs.begin(), defs.end());
    }

    return res;
}

DenseSet<const Instruction*> FlowSensitivePointerAnalysis::findDefFromBB(const BasicBlock *bb, const PointerTy *p, DenseSet<const BasicBlock*> visited){
    
    if(visited.count(bb)){
        return DenseSet<const Instruction*>();
    }
    
    auto lastInst = &(bb->back());

    while(lastInst){
        if(auto callInst = dyn_cast<CallInst>(lastInst)){
            auto defLocs = findDefFromFunc(callInst->getCalledFunction(), p);
            return defLocs;
        }

        if(hasDef(lastInst, p)){
            return DenseSet<const Instruction*>{lastInst};
        }
        lastInst = lastInst->getPrevNonDebugInstruction();
    }
    DenseSet<const Instruction*> res;
    visited.insert(bb);
    for(auto it = pred_begin(bb); it != pred_end(bb); ++it){
        if(visited.find(*it) == visited.end()){
            DenseSet<const Instruction*> defs = findDefFromBB(*it, p, visited);
            res.insert(defs.begin(), defs.end());
        }

    }

    if(res.empty()){
        for(auto callLoc : func2CallerLocation[bb->getParent()]){
            auto defs = FindDefInBasicBlock(callLoc, p);
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

DenseMap<size_t, DenseSet<const Instruction*>> FlowSensitivePointerAnalysis::computePointerLevel(DenseSet<std::pair<const Value*, const Value*>> constraints, DenseMap<const Value *, int> pointers){
    std::vector<std::vector<int>> matrix;

    for(auto con : constraints){

        std::vector<int> line(pointers.size() + 1, 0);
        if(dyn_cast<Instruction>(con.first)){
            line[pointers[con.first]] = -1;
        }
        line[pointers[con.second]] = 1;
        line[pointers.size()] = 1;
        matrix.push_back(line);
    }

    outs() << "Matrix\n";
    for(auto row : matrix){
        for(auto ele : row){
            outs() << ele << " ";
        }
        outs() << "\n";
    }


    for(size_t i = 0; i != constraints.size(); ++i){
        // Find first line that has non zero entry as i-th element.
        size_t j = i;
        while(j != constraints.size() && matrix[j][i] == 0){
            ++j;
        }

        if(j == i){
            int coefficient = (matrix[j][i] > 0 ? 1 : -1);
            for(size_t k = 0; k != pointers.size() + 1; ++k){
                matrix[i][k] = coefficient * matrix[i][k];
            }
        }
        else if(j != constraints.size()){
            int coefficient = (matrix[j][i] > 0 ? 1 : -1);
            int temp;
            for(size_t k = 0; k != pointers.size() + 1; ++k){
                temp = matrix[i][k];
                matrix[i][k] = matrix[j][k] * coefficient;
                matrix[j][k] = temp;
            }
        }
        else{
            // if no such line, then all entry in this column is 0, then we skip to next line.
            continue;
        }

        // add or subtract i-th line.
        for(size_t k = 0; k != constraints.size(); ++k){
            if(k == i){
                continue;
            }

            if(matrix[k][i] == 1){
                // k-th line mins i-th line
                for(size_t h = 0; h != pointers.size() + 1; ++h){
                    matrix[k][h] -= matrix[i][h];
                }
            }
            else if(matrix[k][i] == -1){
                // k-th line plus i-th line
                for(size_t h = 0; h != pointers.size() + 1; ++h){
                    matrix[k][h] += matrix[i][h];
                }
            }
        }
    }

    outs() << "Solved Matrix\n";
    for(auto row : matrix){
        for(auto ele : row){
            outs() << ele << " ";
        }
        outs() << "\n";
    }

    DenseMap<size_t, DenseSet<const Instruction*>> worklist;
    for(auto pointerPair : pointers){
        if(dyn_cast<AllocaInst>(pointerPair.first)){
            outs() << "Insert " << *pointerPair.first << "\n";
            auto tmp = dyn_cast<Instruction>(pointerPair.first);
            std::string str;
            raw_string_ostream(str) << *pointerPair.first;
            str = "Cannot turn " + str + " into an instruction\n";
            outs() << "Transform " << *tmp << "\n";
            assert(tmp && str.c_str());
            outs() << "Pointer level " << matrix[pointerPair.second][pointers.size()] << "\n";
            worklist[matrix[pointerPair.second][pointers.size()]].insert(tmp);
        }
    }

    outs() << "Finish\n";

    return worklist;

}

void FlowSensitivePointerAnalysis::initialize(const Function * const func){
    /*
       Calculate pointer level for each pointer in func

    */

   /*
    Since two pointers used in load or store different in exact 1 points-to level, we do not need to store the 1.
    A pair (a,b) means a + 1 = b
    
   */

    logger.note();
    logger << "Initializing " + func->getName() + "\n";

    // Do not initialize a function twice.
    if(func2worklist.count(func)){
        return;
    }

   DenseSet<std::pair<const Value*, const Value*>> constraints;
   DenseMap<const Value *, int> pointers;
   int counter = 0;
    for(auto &inst : instructions(*func)){
        if(const StoreInst *store = dyn_cast<StoreInst>(&inst)){

            if((dyn_cast<Instruction>(store->getValueOperand()->stripPointerCasts()) && 
                            (!pointers.count(store->getValueOperand()->stripPointerCasts())))){
                pointers[store->getValueOperand()->stripPointerCasts()] = counter++;
            }
            if(!pointers.count(store->getPointerOperand()->stripPointerCasts())){
                pointers[store->getPointerOperand()->stripPointerCasts()] = counter++;
            }
            constraints.insert({store->getValueOperand()->stripPointerCasts(), store->getPointerOperand()->stripPointerCasts()});
        }
        else if(const LoadInst *load = dyn_cast<LoadInst>(&inst)){

            if(!pointers.count(load)){
                pointers[load] = counter++;
            }
            if(!pointers.count(load->getPointerOperand()->stripPointerCasts())){
                pointers[load->getPointerOperand()->stripPointerCasts()] = counter++;
            }

            constraints.insert({load, load->getPointerOperand()->stripPointerCasts()});
        }
        else if(const AllocaInst *alloca = dyn_cast<AllocaInst>(&inst)){
            if(!pointers.count(&inst)){
                pointers[&inst] = counter++;
            }
            labelMap[alloca].insert(Label(&inst, Label::LabelType::Def));
            // Empty points-to set means the pointer is undefined.
            pointsToSet[&inst][&inst] = {DenseSet<const Value*>(), false};
        }
        else if(const CallInst *callInst = dyn_cast<CallInst>(&inst)){
            func2CallerLocation[callInst->getCalledFunction()].insert(&inst);
            caller2Callee[func].insert(callInst->getCalledFunction());
        }
        else if(const ReturnInst *retInst = dyn_cast<ReturnInst>(&inst)){
            func2TerminateBBs[func].insert(retInst->getParent());
        }
    }

    logger.note();
    for(auto pair : constraints){
        logger << "Constraint: " << *pair.first << " + 1 = " << *pair.second << "\n";
    }

    for(auto p : pointers){
        logger << *p.first << " => " << p.second << "\n";
    }


    auto worklist = computePointerLevel(constraints, pointers);

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


#include "llvm/Transforms/Utils/FlowSensitivePointerAnalysis.h"
#include <algorithm>
#include <memory>
#include <exception>
#include <vector>

#define RED_BOLD_PREFIX "\033[1;31m"
#define GREEN_BOLD_PREFIX "\033[1;32m"
#define YELLOW_BOLD_PREFIX "\033[1;33m"
#define ALL_RESET "\033[0m"

using namespace llvm;

AnalysisKey FlowSensitivePointerAnalysis::Key;



/*
    Analysis Entry - perform analysis
*/
FlowSensitivePointerAnalysisResult FlowSensitivePointerAnalysis::run(Module &m, ModuleAnalysisManager &mam){

    getCallGraphFromModule(m);

    // todo: Add support for global variables. There should be a global worklist. 

    auto mainFunctionPtr = getFunctionInCallGrpahByName("main");
    if(!mainFunctionPtr){
        outs() << "Cannot find main function.\n";
        return result;
    } 
    left2Analysis.push(mainFunctionPtr);

    while(!left2Analysis.empty()){
        const Function *cur = left2Analysis.top();
        performPointerAnalysisOnFunction(cur);
        left2Analysis.pop();
    }
    

    result.setPointsToSet(pointsToSet);
    outs() << pointsToSet;

    return result;
}



void FlowSensitivePointerAnalysis::performPointerAnalysisOnFunction(const Function * const func){

    initialize(func);
    outs() << worklist;
    size_t currentPointerLevel = worklist.size();

    while(currentPointerLevel != 0){
        propagate(currentPointerLevel, func);
        // todo: we need a way to set the points-to set changing variable to false.
        --currentPointerLevel;
    }

    
    visited[func] = true;
}

/*
    Build def-use graph and propagate point-to information for pointers of a specific pointer level.
*/
void FlowSensitivePointerAnalysis::propagate(size_t currentPtrLvl, const Function* func){

    for(auto ptr : worklist[currentPtrLvl]){

        #define DEBUG_TYPE "TESTCALLGRAPH"
            LLVM_DEBUG(dbgs() << "Current working pointer:" << *ptr << "\n");
        #undef DEBUG_TYPE


        for(auto user : ptr->users()){
            auto inst = dyn_cast<Instruction>(user);
            #define DEBUG_TYPE "TESTCALLGRAPH"
            LLVM_DEBUG(dbgs() << "Current user: " << *inst << "\n");
            #undef DEBUG_TYPE
            if(auto *storeInst = dyn_cast<StoreInst>(inst)){
                labelMap[inst].insert(Label(storeInst->getPointerOperand(), Label::LabelType::Def));
                labelMap[inst].insert(Label(storeInst->getPointerOperand(), Label::LabelType::Use));
                useList[storeInst->getPointerOperand()].push_back(inst);
            }
            else if(auto *loadInst = dyn_cast<LoadInst>(inst)){
                labelMap[inst].insert(Label(loadInst->getPointerOperand(), Label::LabelType::Use));
                useList[loadInst->getPointerOperand()].push_back(inst);
            }
            else if(auto *callInst = dyn_cast<CallInst>(inst)){
                /*
                    At callsite, we need to tell if we need to analyze the callee by checking the points-to sets of parameters.
                */
                auto callee = callInst->getCalledFunction();
                if(callInst->arg_size()){
                    // Check if any parameters's points-to set is changed.
                    for(auto beg = callInst->arg_begin(), end = callInst->arg_end(); beg != end; ++beg){
                        auto argInst = dyn_cast<Instruction>(beg->get());
                        if(argInst && pointsToSet[inst][argInst].second){
                            break;
                            left2Analysis.push(callee);
                        }
                    }
                }
                else{
                    if(!visited.count(callee)){
                        left2Analysis.push(callee);
                    }
                }

                // todo: we are missing one case that the points-to set of a global variable is changed. If so,
                // we need to push all functions that use that global variable into left2Analysis.
            }
            else if(auto *retInst = dyn_cast<ReturnInst>(inst)){
                /*
                    For any returnInst processed here, it has to be of the form ret %a.
                */
                if(!dyn_cast<AllocaInst>(retInst->getReturnValue())){
                    std::string str;
                    raw_string_ostream(str) << *retInst;
                    str = "The direct handling of return instruction (" + str + ") requires an allocated pointer.\n";
                    llvm_unreachable(str.c_str());
                }
                // fixme, todo : if retInst->getReturnValue() does not alias to a pointer, there is no need to add the use label
                labelMap[inst].insert(Label(retInst->getReturnValue(), Label::LabelType::Use));
                useList[retInst->getReturnValue()].push_back(inst);
            }
            else if(auto *gepInst = dyn_cast<GetElementPtrInst>(inst)){
                #define DEBUG_TYPE "TESTCALLGRAPH"
                LLVM_DEBUG(dbgs() << *gepInst << " is in the user list of pointer " << *ptr << ", but it's neither storeinst nor loadinst.\n");
                #undef DEBUG_TYPE
            }
            else if(auto *bitcastInst = dyn_cast<BitCastInst>(inst)){
                #define DEBUG_TYPE "TESTCALLGRAPH"
                LLVM_DEBUG(dbgs() << *bitcastInst << " is in the user list of pointer " << *ptr << ", but it's neither storeinst nor loadinst.\n");
                #undef DEBUG_TYPE
            }
            else if(auto *cmpInst = dyn_cast<CmpInst>(inst)){
                #define DEBUG_TYPE "TESTCALLGRAPH"
                LLVM_DEBUG(dbgs() << *cmpInst << " is in the user list of pointer " << *ptr << ", but it's neither storeinst nor loadinst.\n");
                #undef DEBUG_TYPE
            }
            else{
                std::string str, str0;
                raw_string_ostream(str0) << *inst;

                str = "Cannot process instruction:" + str0 + "\n";
                llvm_unreachable(str.c_str());
            }
        }


        

        for(auto useLoc : useList[ptr]){
            #define DEBUG_TYPE "TESTCALLGRAPH"
            LLVM_DEBUG(dbgs() << "Finding def for " << *useLoc << "\n");
            #undef DEBUG_TYPE

            auto pd = findDefFromUse(useLoc, ptr);
            for(auto def : pd){
                addDefUseEdge(def, useLoc, ptr);
                #define DEBUG_TYPE "TESTCALLGRAPH"
                    LLVM_DEBUG(dbgs() << "Add def use edge: " << *def << "=== " << *ptr << " ===>" << *useLoc << "\n");
                #undef DEBUG_TYPE
            }
        }

        // outs() << defUseGraph;


        auto initialDUEdges = getAffectUseLocations(ptr, ptr);
        // outs() << initialDUEdges.size() << "\n";


        std::vector<DefUseEdgeTupleTy> propagateList;
        for(auto pu : initialDUEdges){
            propagateList.push_back(std::make_tuple(ptr, pu, ptr));
        }


        while(!propagateList.empty()){
            auto tup = propagateList.front();
            auto f = std::get<0>(tup);
            auto t = std::get<1>(tup);
            auto ptr = std::get<2>(tup);

            #define DEBUG_TYPE "TESTCALLGRAPH"
            LLVM_DEBUG(dbgs() << "Propagating along def use edge: " << *f << " ===== " << *ptr << " ====> " << *t << "\n");
            #undef DEBUG_TYPE

            propagatePointsToInformation(t, f, ptr);

            if(auto storeInst = dyn_cast<StoreInst>(t)){
                auto pts = calculatePointsToInformationForStoreInst(t, storeInst);
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

                    for(auto user : aliasUser[t]){
                        
                        auto userInst = dyn_cast<Instruction>(user);
                        if(auto storeInst = dyn_cast<StoreInst>(userInst)){
                            if(t == storeInst->getPointerOperand()){
                                aliasMap[userInst][t] = aliasMap[t][t];

                                if(auto ptr = dyn_cast<Instruction>(storeInst->getValueOperand())){
                                    if(aliasMap[userInst][ptr].empty()){
                                        updatePointsToSet(userInst, t, std::set<const Value*>{ptr}, propagateList);
                                        // pointsToSet[userInst][t].first = std::set<const Value*>{ptr};
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
                                    updatePointsToSet(userInst, t, std::set<const Value*>{storeInst->getValueOperand()}, propagateList);
                                    // pointsToSet[userInst][t].first = std::set<const Value*>{storeInst->getValueOperand()};
                                }


                                for(auto tt : aliasMap[userInst][t]){
                                    labelMap[userInst].insert(Label(tt, Label::LabelType::Def));
                                    labelMap[userInst].insert(Label(tt, Label::LabelType::Use));
                                    useList[tt].push_back(userInst);

                                }
                                
                            }
                            else if(t == storeInst->getValueOperand()){
                                aliasMap[userInst][t] = aliasMap[t][t];
                                auto ptrChangeList = ptsPointsTo(userInst,t);
                                for(auto p0 : ptrChangeList){
                                    auto p1 = dyn_cast<Instruction>(p0);
                                    pointsToSet[userInst][p1].first = aliasMap[userInst][t];

                                    // Here, if points2set is changed, we need to propagate.

                                    auto tmp = pointsToSet[userInst][p1];
                                    errs() << "points-to set changed.\n";
                                    auto passList = getAffectUseLocations(userInst, p1);
                                    for(auto u : passList){    
                                            propagateList.push_back(std::make_tuple(userInst,u,p1));
                                            errs() << "New def use edge added to propagatelist: " << *userInst << "=== " << *p1 << " ===>" << *u << "\n";      

                                    }
                                }
                            }
                            else{
                                errs() << "Hitting at " << *storeInst << " with pointer " << *t << "\n";
                            }
                        }
                        else if(auto pt0 = dyn_cast<LoadInst>(userInst)){
                            aliasMap[userInst][t] = aliasMap[t][t];
                            for(auto tt : aliasMap[userInst][t]){
                                labelMap[userInst].insert(Label(tt, Label::LabelType::Use));
                                useList[tt].push_back(userInst);
                            }

                        }
                        else if(auto pt0 = dyn_cast<ReturnInst>(userInst)){
                            aliasMap[userInst][t] = aliasMap[t][t];
                            for(auto tt : aliasMap[userInst][t]){
                                if(dyn_cast<AllocaInst>(tt) || dyn_cast<LoadInst>(tt)){
                                    labelMap[userInst].insert(Label(tt, Label::LabelType::Use));
                                    useList[tt].push_back(userInst);
                                }
                                
                            }
                        }
                        else{
                            #define DEBUG_TYPE "TESTCALLGRAPH"
                            LLVM_DEBUG(dbgs() << "Wrong clause type: " << *userInst << "\n");
                            #undef DEBUG_TYPE
                        }

                    }
                }
            }
            propagateList.erase(propagateList.begin());

            // dumpLabelMap();
            // dumpPointsToMap();
        }

    }
}

void FlowSensitivePointerAnalysis::updatePointsToSet(const ProgramLocationTy *loc, const PointerTy *ptr, std::set<const Value *> pts, std::vector<DefUseEdgeTupleTy> &propagateList){
    auto tmp = pointsToSet[loc][ptr].first;
    pointsToSet[loc][ptr].first = pts;
    // Here, if points2set is changed, we need to propagate.
    if(tmp != pointsToSet[loc][ptr].first){
        pointsToSet[loc][ptr].second = true;
        #define DEBUG_TYPE "TESTCALLGRAPH"
        LLVM_DEBUG(dbgs() << "points-to set changed.\n");
        #undef DEBUG_TYPE
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

std::set<const Value*> FlowSensitivePointerAnalysis::getAlias(const ProgramLocationTy *t, const Instruction *p){
    // for a store inst "store a b", we get the alias set of a at t.
    if(auto pt = dyn_cast<StoreInst>(p)){
        std::set<const Value*> pointees = aliasMap[t][pt->getValueOperand()];
        if(pointees.empty()){
            return std::set<const Value*>{dyn_cast<Instruction>(pt->getValueOperand())};
        }
        return pointees;
    }
    // for a "a = load b", we get the alias set of b at t.
    else if(auto pt = dyn_cast<LoadInst>(p)){
        std::set<const Value*> pointees = aliasMap[t][pt->getPointerOperand()];
        if(pointees.empty()){
            return std::set<const Value*>{dyn_cast<Instruction>(pt->getPointerOperand())};
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


std::set<const Value*> FlowSensitivePointerAnalysis::calculatePointsToInformationForStoreInst(const ProgramLocationTy *t, const StoreInst *pt){
    std::set<const Value*> pointees = aliasMap[t][pt->getValueOperand()];
    return (pointees.empty() ? std::set<const Value*>{pt->getValueOperand()->stripPointerCasts()} : pointees);
    // if(pointees.empty()){
    //     pointsToSet[t][pvar].first = std::set<const Value*>{pt->getValueOperand()->stripPointerCasts()};
    // }
    // else{
    //     pointsToSet[t][pvar].first = pointees;
    // }
    
    // return;
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



std::vector<const Instruction*> FlowSensitivePointerAnalysis::findDefFromUse(const ProgramLocationTy *loc, const PointerTy *ptr){
    while(true){
        /*
            LLVM has some intrinsic functions for mapping between LLVM program objects and the source-level objects. These debug instructions are not related to our analysis.
        */
        auto prevLoc = loc->getPrevNonDebugInstruction();
        if(prevLoc){
            if(hasDef(prevLoc, ptr)){
                return std::vector<const Instruction*>{prevLoc};
            }
            loc = prevLoc;
        }
        else{
            std::vector<const ProgramLocationTy*> res;
            for(auto it = pred_begin(loc->getParent()); it != pred_end(loc->getParent()); ++it){
                std::vector<const Instruction*> defs = findDefFromBB(*it, ptr);
                res.insert(res.end(), defs.begin(), defs.end());
            }
            return res;
        }
    }
}

std::vector<const Instruction*> FlowSensitivePointerAnalysis::findDefFromBB(const BasicBlock *bb, const PointerTy *p){
    auto lastInst = &(bb->back());
    while(lastInst){
        if(hasDef(lastInst, p)){
            return std::vector<const Instruction*>{lastInst};
        }
        lastInst = lastInst->getPrevNonDebugInstruction();
    }
    std::vector<const Instruction*> res;
    for(auto it = pred_begin(bb); it != pred_end(bb); ++it){
        std::vector<const Instruction*> defs = findDefFromBB(*it, p);
        res.insert(res.end(), defs.begin(), defs.end());
    }
    return res;
}


bool FlowSensitivePointerAnalysis::hasDef(const ProgramLocationTy *loc, const PointerTy *ptr){
    auto iter = std::find_if(labelMap[loc].begin(), labelMap[loc].end(), [&](Label l) -> bool {
        return l.type == Label::LabelType::Def && l.ptr == ptr;
        });

    return (iter == labelMap[loc].end() ? false : true);
}

void FlowSensitivePointerAnalysis::initialize(const Function * const func){
    /*
        If we can assume all allocaInsts are in the first basicblock, we can only search the first basicblock for allocaInsts.
        Update 2024-03-27: Generally we cannot assume that is what happening. Imagine a for loop :(
        Update 2024-04-03: LLVM-17 removes the support of typed pointer with opaque pointer. Now we no longer have access to the detailed 
                            type to a pointer. So we would need some other way to calculate pointer level.
    */

   /*
    Since two pointers used in load or store different in exact 1 points-to level, we do not need to store the 1.
    A pair (a,b) means a + 1 = b

    a = b + 1
    c = a + 1

    b = 1

    a b c
    1 -1 0 1
    -1 0 1 1
    0 1 0 1

    1 0 0 2
    0 1 0 1
    0 -1 1 2

    1 0 0 2
    0 1 0 1
    0 0 1 3

    a = 2, b = 1, c = 3
    
   */
   std::set<std::pair<const Value*, const Value*>> constraints;
   std::map<const Value *, int> pointers;
   int counter = 0;
    for(auto &inst : instructions(*func)){

        if(const StoreInst *store = dyn_cast<StoreInst>(&inst)){
            constraints.insert({store->getValueOperand()->stripPointerCasts(), store->getPointerOperand()->stripPointerCasts()});
        }
        else if(const LoadInst *load = dyn_cast<LoadInst>(&inst)){
            constraints.insert({load, load->getPointerOperand()->stripPointerCasts()});
            if(!pointers.count(&inst)){
                pointers[&inst] = counter++;
            }
        }
        else if(const AllocaInst *alloca = dyn_cast<AllocaInst>(&inst)){
            if(!pointers.count(&inst)){
                pointers[&inst] = counter++;
            }
        }
    }


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

    // for(auto line : matrix){
    //     for(auto num : line){
    //         errs() << num << " ";
    //     }
    //     errs() << "\n";
    // }

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

    // for(auto p : pointers){
    //     errs() << *(p.first) << " " << p.second << "\n";
    // }
    // for(auto line : matrix){
    //     for(auto num : line){
    //         errs() << num << " ";
    //     }
    //     errs() << "\n";
    // }
    for(auto pointerPair : pointers){
        if(dyn_cast<AllocaInst>(pointerPair.first)){
            worklist[matrix[pointerPair.second][pointers.size()]].insert(dyn_cast<Instruction>(pointerPair.first));
        }
    }

    for(auto &inst : instructions(*func)){
        if(const AllocaInst *alloca = dyn_cast<AllocaInst>(&inst)){
            labelMap[alloca].insert(Label(&inst, Label::LabelType::Def));
            // Empty points-to set means the pointer is undefined.
            pointsToSet[&inst][&inst] = {std::set<const Value*>(), false};
        }
    }

    result.setWorkList(worklist);
    // todo: for each call instruction, we need to add auxiliary instructions to enable pointer arguments passing among themselves.
    // For example, for function f(int *a, int *b) that returns int *r and callsite int *ret = f(x,y);, 
    // we need a = x, b = y at the very beginning of the function, and ret = r at the end of the function.
}


const Function* FlowSensitivePointerAnalysis::getFunctionInCallGrpahByName(std::string name){
    using FunctionMapValueType = std::pair<const Function *const, std::unique_ptr<CallGraphNode>>;

    /*
        We have to test if the first part is nullptr when traversing the callgraph. The callgraph uses nullptr to represent an
        ExtrenalCallingNode or a CallsExternalNode. For example, for main function, dumping the callgraph will show "CS<None> 
        calls function 'main'"
    */
    auto res = std::find_if(cg->begin(), cg->end(), [name](FunctionMapValueType &p) -> bool {
        return p.first && p.first->getName() == name;
        });

    return (res == cg->end() ? nullptr : res->first);
}







void FlowSensitivePointerAnalysis::dumpWorkList(){
    // DenseMap<size_t, DenseSet<const Instruction*>> worklist;

    for(auto beg = worklist.begin(), end = worklist.end(); beg != end; ++beg){
        size_t ptrLevel = beg->first;
        errs() << "Pointer level: " << ptrLevel << ":\n";
        DenseSet<const Instruction*> elements = beg->second;
        for(auto b = elements.begin(), e = elements.end(); b != e; ++b){
            errs() << *(*b) << "\n";
        }
    }

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
    errs() << "Dump Def-use graph.\n";
    #define DEBUG_STR_LENGTH 30
    errs() << "\n" << std::string(DEBUG_STR_LENGTH, '*') << RED_BOLD_PREFIX "def use graph" ALL_RESET << std::string(DEBUG_STR_LENGTH, '*') << "\n";
    for(auto it = defUseGraph.begin(); it != defUseGraph.end(); ++it){
        for(auto iter = it->second.begin(), end = it->second.end(); iter != end; ++iter){
            auto ptr = iter->first;
            for(auto i = iter->second.begin(), e = iter->second.end(); i != e; ++i){
                errs() << YELLOW_BOLD_PREFIX << *(it->first) << ALL_RESET " === " << **i << " ===> " << *ptr << "\n";
            }
        }
        // for(auto ptr : it->second->second){
        //     errs() << YELLOW_BOLD_PREFIX << *(it->first) << ALL_RESET " === ";
        // }
        // ====> " GREEN_BOLD_PREFIX << ALL_RESET << "\n";
    }
    errs() << std::string(DEBUG_STR_LENGTH * 2, '*') << "\n\n";
    #undef DEBUG_STR_LENGTH
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


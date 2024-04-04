#include "TestCallGraph.h"
#include <algorithm>

#define RED_BOLD_PREFIX "\033[1;31m"
#define GREEN_BOLD_PREFIX "\033[1;32m"
#define YELLOW_BOLD_PREFIX "\033[1;33m"
#define ALL_RESET "\033[0m"


/*
    Analysis Entry - perform analysis
*/
bool TestCallGraphWrapper::runOnModule(llvm::Module &m){

    getCallGraphFromModule(m);

    // todo: Add support for global variables. There should be a global worklist. 

    auto mainFunctionPtr = getFunctionInCallGrpahByName("main");
    if(!mainFunctionPtr){
        llvm::errs() << "Cannot find main function.\n";
        return false;
    } 
    left2Analysis.push(mainFunctionPtr);

    while(!left2Analysis.empty()){
        const llvm::Function *cur = left2Analysis.top();
        performPointerAnalysisOnFunction(cur);
        left2Analysis.pop();
    }

    return false;
}



void TestCallGraphWrapper::performPointerAnalysisOnFunction(const llvm::Function * const func){

    initialize(func);
    // size_t currentPointerLevel = worklist.size();

    // while(currentPointerLevel != 0){
    //     propagate(currentPointerLevel, func);
    //     // todo: we need a way to set the points-to set changing variable to false.
    //     --currentPointerLevel;
    // }

    // visited[func] = true;
}

/*
    Build def-use graph and propagate point-to information for pointers of a specific pointer level.
*/
void TestCallGraphWrapper::propagate(size_t currentPtrLvl, const llvm::Function* func){

    for(auto ptr : worklist[currentPtrLvl]){

        #define DEBUG_TYPE "TESTCALLGRAPH"
            LLVM_DEBUG(llvm::dbgs() << "Current working pointer:" << *ptr << "\n");
        #undef DEBUG_TYPE


        for(auto user : ptr->users()){
            auto inst = llvm::dyn_cast<llvm::Instruction>(user);
            #define DEBUG_TYPE "TESTCALLGRAPH"
            LLVM_DEBUG(llvm::dbgs() << "Current user: " << *inst << "\n");
            #undef DEBUG_TYPE
            if(auto *storeInst = llvm::dyn_cast<llvm::StoreInst>(inst)){
                labelMap[inst].insert(Label(storeInst->getPointerOperand(), Label::LabelType::Def));
                labelMap[inst].insert(Label(storeInst->getPointerOperand(), Label::LabelType::Use));
                useList[storeInst->getPointerOperand()].push_back(inst);
            }
            else if(auto *loadInst = llvm::dyn_cast<llvm::LoadInst>(inst)){
                labelMap[inst].insert(Label(loadInst->getPointerOperand(), Label::LabelType::Use));
                useList[loadInst->getPointerOperand()].push_back(inst);
            }
            else if(auto *callInst = llvm::dyn_cast<llvm::CallInst>(inst)){
                /*
                    At callsite, we need to tell if we need to analyze the callee by checking the points-to sets of parameters.
                */
                auto callee = callInst->getCalledFunction();
                if(callInst->arg_size()){
                    // Check if any parameters's points-to set is changed.
                    for(auto beg = callInst->arg_begin(), end = callInst->arg_end(); beg != end; ++beg){
                        auto argInst = llvm::dyn_cast<llvm::Instruction>(beg->get());
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
            else if(auto *retInst = llvm::dyn_cast<llvm::ReturnInst>(inst)){
                /*
                    For any returnInst processed here, it has to be of the form ret %a.
                */
                if(!llvm::dyn_cast<llvm::AllocaInst>(retInst->getReturnValue())){
                    std::string str;
                    llvm::raw_string_ostream(str) << *retInst;
                    str = "The direct handling of return instruction (" + str + ") requires an allocated pointer.\n";
                    llvm_unreachable(str.c_str());
                }
                // fixme, todo : if retInst->getReturnValue() does not alias to a pointer, there is no need to add the use label
                labelMap[inst].insert(Label(retInst->getReturnValue(), Label::LabelType::Use));
                useList[retInst->getReturnValue()].push_back(inst);
            }
            else if(auto *gepInst = llvm::dyn_cast<llvm::GetElementPtrInst>(inst)){
                #define DEBUG_TYPE "TESTCALLGRAPH"
                LLVM_DEBUG(llvm::dbgs() << *gepInst << " is in the user list of pointer " << *ptr << ", but it's neither storeinst nor loadinst.\n");
                #undef DEBUG_TYPE
            }
            else if(auto *bitcastInst = llvm::dyn_cast<llvm::BitCastInst>(inst)){
                #define DEBUG_TYPE "TESTCALLGRAPH"
                LLVM_DEBUG(llvm::dbgs() << *bitcastInst << " is in the user list of pointer " << *ptr << ", but it's neither storeinst nor loadinst.\n");
                #undef DEBUG_TYPE
            }
            else{
                std::string str, str0;
                llvm::raw_string_ostream(str0) << *inst;

                str = "Cannot process instruction:" + str0 + "\n";
                llvm_unreachable(str.c_str());
            }
        }


        

        for(auto useLoc : useList[ptr]){
            #define DEBUG_TYPE "TESTCALLGRAPH"
            LLVM_DEBUG(llvm::dbgs() << "Finding def for " << *useLoc << "\n");
            #undef DEBUG_TYPE

            auto pd = findDefFromUse(useLoc, ptr);
            for(auto def : pd){
                addDefUseEdge(def, useLoc, ptr);
                #define DEBUG_TYPE "TESTCALLGRAPH"
                    LLVM_DEBUG(llvm::dbgs() << "Add def use edge: " << *def << "=== " << *ptr << " ===>" << *useLoc << "\n");
                #undef DEBUG_TYPE
            }
        }


        auto initialDUEdges = getAffectUseLocations(ptr, ptr);


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
            LLVM_DEBUG(llvm::dbgs() << "Propagating along def use edge: " << *f << " ===== " << *ptr << " ====> " << *t << "\n");
            #undef DEBUG_TYPE

            propagatePointsToInformation(t, f, ptr);

            if(auto storeInst = llvm::dyn_cast<llvm::StoreInst>(t)){
                auto pts = calculatePointsToInformationForStoreInst(t, storeInst);
                updatePointsToSet(t, ptr, pts, propagateList);
            }
            else if(auto loadInst = llvm::dyn_cast<llvm::LoadInst>(t)){
                auto tmp = aliasMap[t][t];
                updateAliasInformation(t,loadInst);
                // Everytime we update the alias information for pointer pt at location t, we need to add the program locaton t to the users of pt.
                if(tmp != aliasMap[t][t]){
                    if(!aliasUser.count(t)){
                        aliasUser[t] = std::set<const llvm::User*>();
                        for(auto user : t->users()){
                            aliasUser[t].insert(user);
                        }
                    }

                    for(auto user : aliasUser[t]){
                        
                        auto userInst = llvm::dyn_cast<llvm::Instruction>(user);
                        if(auto storeInst = llvm::dyn_cast<llvm::StoreInst>(userInst)){
                            if(t == storeInst->getPointerOperand()){
                                aliasMap[userInst][t] = aliasMap[t][t];

                                if(auto ptr = llvm::dyn_cast<llvm::Instruction>(storeInst->getValueOperand())){
                                    if(aliasMap[userInst][ptr].empty()){
                                        updatePointsToSet(userInst, t, std::set<const llvm::Value*>{ptr}, propagateList);
                                        // pointsToSet[userInst][t].first = std::set<const llvm::Value*>{ptr};
                                        for(auto tt0 : aliasMap[userInst][t]){
                                            auto tt = llvm::dyn_cast<llvm::Instruction>(tt0);
                                            updatePointsToSet(userInst, tt, pointsToSet[userInst][t].first, propagateList);
                                        }
                                    }
                                    else{
                                        pointsToSet[userInst][t].first = aliasMap[userInst][storeInst->getValueOperand()];
                                        for(auto tt0 : aliasMap[userInst][t]){
                                            auto tt = llvm::dyn_cast<llvm::Instruction>(tt0);
                                            updatePointsToSet(userInst, tt, pointsToSet[userInst][t].first, propagateList);
                                        }
                                    }
                                }
                                else{
                                    updatePointsToSet(userInst, t, std::set<const llvm::Value*>{storeInst->getValueOperand()}, propagateList);
                                    // pointsToSet[userInst][t].first = std::set<const llvm::Value*>{storeInst->getValueOperand()};
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
                                    auto p1 = llvm::dyn_cast<llvm::Instruction>(p0);
                                    pointsToSet[userInst][p1].first = aliasMap[userInst][t];

                                    // Here, if points2set is changed, we need to propagate.

                                    auto tmp = pointsToSet[userInst][p1];
                                    llvm::errs() << "points-to set changed.\n";
                                    auto passList = getAffectUseLocations(userInst, p1);
                                    for(auto u : passList){    
                                            propagateList.push_back(std::make_tuple(userInst,u,p1));
                                            llvm::errs() << "New def use edge added to propagatelist: " << *userInst << "=== " << *p1 << " ===>" << *u << "\n";      

                                    }
                                }
                            }
                            else{
                                llvm::errs() << "Hitting at " << *storeInst << " with pointer " << *t << "\n";
                            }
                        }
                        else if(auto pt0 = llvm::dyn_cast<llvm::LoadInst>(userInst)){
                            aliasMap[userInst][t] = aliasMap[t][t];
                            for(auto tt : aliasMap[userInst][t]){
                                labelMap[userInst].insert(Label(tt, Label::LabelType::Use));
                                useList[tt].push_back(userInst);
                            }

                        }
                        else if(auto pt0 = llvm::dyn_cast<llvm::ReturnInst>(userInst)){
                            aliasMap[userInst][t] = aliasMap[t][t];
                            for(auto tt : aliasMap[userInst][t]){
                                if(llvm::dyn_cast<llvm::AllocaInst>(tt) || llvm::dyn_cast<llvm::LoadInst>(tt)){
                                    labelMap[userInst].insert(Label(tt, Label::LabelType::Use));
                                    useList[tt].push_back(userInst);
                                }
                                
                            }
                        }
                        else{
                            #define DEBUG_TYPE "TESTCALLGRAPH"
                            LLVM_DEBUG(llvm::dbgs() << "Wrong clause type: " << *userInst << "\n");
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

void TestCallGraphWrapper::updatePointsToSet(const ProgramLocationTy *loc, const PointerTy *ptr, std::set<const llvm::Value *> pts, std::vector<DefUseEdgeTupleTy> &propagateList){
    auto tmp = pointsToSet[loc][ptr].first;
    pointsToSet[loc][ptr].first = pts;
    // Here, if points2set is changed, we need to propagate.
    if(tmp != pointsToSet[loc][ptr].first){
        pointsToSet[loc][ptr].second = true;
        #define DEBUG_TYPE "TESTCALLGRAPH"
        LLVM_DEBUG(llvm::dbgs() << "points-to set changed.\n");
        #undef DEBUG_TYPE
        auto affectedUseLocs = getAffectUseLocations(loc, ptr);
        for(auto u : affectedUseLocs){    
                propagateList.push_back(std::make_tuple(loc,u,ptr));
                #define DEBUG_TYPE "TESTCALLGRAPH"
                LLVM_DEBUG(llvm::dbgs() << "New def use edge added to propagatelist: " << *loc << "=== " << *ptr << " ===>" << *u << "\n");
                #undef DEBUG_TYPE
        }
    }
    
}

void TestCallGraphWrapper::updateAliasInformation(const ProgramLocationTy *loc, const llvm::LoadInst *loadInst){
    auto aliases = getAlias(loc, loadInst);

    for(auto &b : aliases){
        auto pointer = llvm::dyn_cast<llvm::Instruction>(b);
        if(!aliasUser.count(pointer)){
            aliasUser[pointer] = std::set<const llvm::User*>();
            for(auto user0 : pointer->users()){
                aliasUser[pointer].insert(user0);
            }
        }
        auto user = llvm::dyn_cast<llvm::User>(loc);
        aliasUser[pointer].insert(user);


        auto a = llvm::dyn_cast<llvm::Instruction>(b);
        aliasMap[loc][loc].insert(pointsToSet[loc][a].first.begin(), pointsToSet[loc][a].first.end());
    }
    
    return;
}

std::set<const llvm::Value*> TestCallGraphWrapper::getAlias(const ProgramLocationTy *t, const const llvm::Instruction *p){
    // for a store inst "store a b", we get the alias set of a at t.
    if(auto pt = llvm::dyn_cast<llvm::StoreInst>(p)){
        std::set<const llvm::Value*> pointees = aliasMap[t][pt->getValueOperand()];
        if(pointees.empty()){
            return std::set<const llvm::Value*>{llvm::dyn_cast<llvm::Instruction>(pt->getValueOperand())};
        }
        return pointees;
    }
    // for a "a = load b", we get the alias set of b at t.
    else if(auto pt = llvm::dyn_cast<llvm::LoadInst>(p)){
        std::set<const llvm::Value*> pointees = aliasMap[t][pt->getPointerOperand()];
        if(pointees.empty()){
            return std::set<const llvm::Value*>{llvm::dyn_cast<llvm::Instruction>(pt->getPointerOperand())};
        }
        return pointees;
    }
}


std::set<const llvm::Value*> TestCallGraphWrapper::calculatePointsToInformationForStoreInst(const ProgramLocationTy *t, const llvm::StoreInst *pt){
    std::set<const llvm::Value*> pointees = aliasMap[t][pt->getValueOperand()];
    return (pointees.empty() ? std::set<const llvm::Value*>{pt->getValueOperand()->stripPointerCasts()} : pointees);
    // if(pointees.empty()){
    //     pointsToSet[t][pvar].first = std::set<const llvm::Value*>{pt->getValueOperand()->stripPointerCasts()};
    // }
    // else{
    //     pointsToSet[t][pvar].first = pointees;
    // }
    
    // return;
}


std::vector<const TestCallGraphWrapper::ProgramLocationTy*> TestCallGraphWrapper::getAffectUseLocations(const ProgramLocationTy *loc, const llvm::Instruction *ptr){
    std::vector<const llvm::Instruction*> res;

    for(auto iter = defUseGraph[loc].begin(); iter != defUseGraph[loc].end(); ++iter){
        auto it = std::find_if(iter->second.begin(), iter->second.end(), [ptr](const PointerTy *p) -> bool{
            return p == ptr;
        });
        if(it != iter->second.end()){
            res.push_back(iter->first);
        }
        // for(auto it = iter->second.begin(); it != iter->second.end(); ++it){
        //     if(*it == ptr){
        //         res.push_back(iter->first);
        //     }
        // }
    }
        
    return res;
}


void TestCallGraphWrapper::propagatePointsToInformation(const ProgramLocationTy *toLoc, const ProgramLocationTy *fromLoc, const PointerTy *var){
    auto oldPointsToSet = pointsToSet[toLoc][var].first;
    pointsToSet[toLoc][var].first.insert(pointsToSet[fromLoc][var].first.begin(), pointsToSet[fromLoc][var].first.end());
    if(pointsToSet[toLoc][var].first != oldPointsToSet){
        pointsToSet[toLoc][var].second = true;
    }
    return;
}


void TestCallGraphWrapper::addDefUseEdge(const ProgramLocationTy *def, const ProgramLocationTy *use, const PointerTy *ptr){
    defUseGraph[def][ptr].insert(use);
}



std::vector<const llvm::Instruction*> TestCallGraphWrapper::findDefFromUse(const ProgramLocationTy *loc, const PointerTy *ptr){
    while(true){
        /*
            LLVM has some intrinsic functions for mapping between LLVM program objects and the source-level objects. These debug instructions are not related to our analysis.
        */
        auto prevLoc = loc->getPrevNonDebugInstruction();
        if(prevLoc){
            if(hasDef(prevLoc, ptr)){
                return std::vector<const llvm::Instruction*>{prevLoc};
            }
            loc = prevLoc;
        }
        else{
            std::vector<const ProgramLocationTy*> res;
            for(auto it = pred_begin(loc->getParent()); it != pred_end(loc->getParent()); ++it){
                std::vector<const llvm::Instruction*> defs = findDefFromBB(*it, ptr);
                res.insert(res.end(), defs.begin(), defs.end());
            }
            return res;
        }
    }
}

std::vector<const llvm::Instruction*> TestCallGraphWrapper::findDefFromBB(const llvm::BasicBlock *bb, const PointerTy *p){
    auto lastInst = &(bb->back());
    while(lastInst){
        if(hasDef(lastInst, p)){
            return std::vector<const llvm::Instruction*>{lastInst};
        }
        lastInst = lastInst->getPrevNonDebugInstruction();
    }
    std::vector<const llvm::Instruction*> res;
    for(auto it = pred_begin(bb); it != pred_end(bb); ++it){
        std::vector<const llvm::Instruction*> defs = findDefFromBB(*it, p);
        res.insert(res.end(), defs.begin(), defs.end());
    }
    return res;
}


bool TestCallGraphWrapper::hasDef(const ProgramLocationTy *loc, const PointerTy *ptr){
    auto iter = std::find_if(labelMap[loc].begin(), labelMap[loc].end(), [&](Label l) -> bool {
        return l.type == Label::LabelType::Def && l.ptr == ptr;
        });

    return (iter == labelMap[loc].end() ? false : true);
}

void TestCallGraphWrapper::initialize(const llvm::Function * const func){
    /*
        If we can assume all allocaInsts are in the first basicblock, we can only search the first basicblock for allocaInsts.
        Update 2024-03-27: Generally we cannot assume that is what happening. Imagine a for loop :(
    */
    for(auto &inst : llvm::instructions(*func)){
        if(const llvm::AllocaInst *alloca = llvm::dyn_cast<llvm::AllocaInst>(&inst)){
            llvm::errs() << *(alloca->getAllocatedType()) << "\n";
            // labelMap[alloca].insert(Label(&inst, Label::LabelType::Def));
            // size_t ptrLvl = countPointerLevel(alloca);
            // worklist[ptrLvl].insert(&inst);
            // // Empty points-to set means the pointer is undefined.
            // pointsToSet[&inst][&inst] = {std::set<const llvm::Value*>(), false};
        }
    }
    // todo: for each call instruction, we need to add auxiliary instructions to enable pointer arguments passing among themselves.
    // For example, for function f(int *a, int *b) that returns int *r and callsite int *ret = f(x,y);, 
    // we need a = x, b = y at the very beginning of the function, and ret = r at the end of the function.
}

size_t TestCallGraphWrapper::countPointerLevel(const llvm::AllocaInst *allocaInst){
    /*
        Pointer level describes how deep a pointer is. 
        The pointer level for a pointer that points to a non-pointer is 1, and if pl(a) = 1, any pointer that can points-to a without 
        casting has a pointer level of 2. 
    */
    size_t pointerLevel = 1;

    auto ty = allocaInst->getAllocatedType();
    while(ty->isPointerTy()){
        ++pointerLevel;
        ty = ty->getNonOpaquePointerElementType();
    }
    return pointerLevel;
}


const llvm::Function* TestCallGraphWrapper::getFunctionInCallGrpahByName(std::string name){
    using FunctionMapValueType = std::pair<const llvm::Function *const, std::unique_ptr<llvm::CallGraphNode>>;

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







void TestCallGraphWrapper::dumpWorkList(){
    // llvm::DenseMap<size_t, llvm::DenseSet<const llvm::Instruction*>> worklist;

    for(auto beg = worklist.begin(), end = worklist.end(); beg != end; ++beg){
        size_t ptrLevel = beg->first;
        llvm::errs() << "Pointer level: " << ptrLevel << ":\n";
        llvm::DenseSet<const llvm::Instruction*> elements = beg->second;
        for(auto b = elements.begin(), e = elements.end(); b != e; ++b){
            llvm::errs() << *(*b) << "\n";
        }
    }

}

void TestCallGraphWrapper::dumpLabelMap(){
    llvm::errs() << "Dump Labelmap.\n";
    for(auto beg = labelMap.begin(), end = labelMap.end(); beg != end; ++beg){
        llvm::errs() << *(beg->first) << "\n";
        for(auto b = beg->second.begin(), e = beg->second.end(); b != e; ++b){
            llvm::errs() << "\t" << *b << "\n";
        }
    }
}

void TestCallGraphWrapper::dumpPointsToMap(){
    llvm::errs() << "Dump points-to map.\n";
    for(auto beg = pointsToSet.begin(), end = pointsToSet.end(); beg != end; ++beg){
        auto inst = beg->first;
        llvm::errs() << "\tAt program location: " << *inst << ":\n";
        for(auto b = beg->second.begin(), e = beg->second.end(); b != e; ++b){
            auto ptr = b->first;
            auto pointees = b->second;
            llvm::errs() << "\t" << *ptr << " ==>\n";
            if(pointees.first.empty()){
                llvm::errs() << "\t\tundefined\n";
            }
            else{
                for(auto pointee : pointees.first){
                    if(pointee){
                        llvm::errs() << "\t\t" << *pointee << "\n";
                    }
                    else{
                        llvm::errs() << "\t\tinvalid pointer" << "\n";
                    }
            }
            }
            
        }
        for(auto b0 = aliasMap[inst].begin(), e0 = aliasMap[inst].end(); b0 != e0; ++b0){
            for(auto a : b0->second){
                if(a){
                    llvm::errs() << "\t" << *b0->first << " is alias to " << *a << "\n";
                }
            }
        }
    }

}


void TestCallGraphWrapper::dumpDefUseGraph() const{
    llvm::errs() << "Dump Def-use graph.\n";
    #define DEBUG_STR_LENGTH 30
    llvm::errs() << "\n" << std::string(DEBUG_STR_LENGTH, '*') << RED_BOLD_PREFIX "def use graph" ALL_RESET << std::string(DEBUG_STR_LENGTH, '*') << "\n";
    for(auto it = defUseGraph.begin(); it != defUseGraph.end(); ++it){
        for(auto iter = it->second.begin(), end = it->second.end(); iter != end; ++iter){
            auto ptr = iter->first;
            for(auto i = iter->second.begin(), e = iter->second.end(); i != e; ++i){
                llvm::errs() << YELLOW_BOLD_PREFIX << *(it->first) << ALL_RESET " === " << **i << " ===> " << *ptr << "\n";
            }
        }
        // for(auto ptr : it->second->second){
        //     llvm::errs() << YELLOW_BOLD_PREFIX << *(it->first) << ALL_RESET " === ";
        // }
        // ====> " GREEN_BOLD_PREFIX << ALL_RESET << "\n";
    }
    llvm::errs() << std::string(DEBUG_STR_LENGTH * 2, '*') << "\n\n";
    #undef DEBUG_STR_LENGTH
}

bool TestCallGraphWrapper::notVisited(const llvm::Function *f){

}

std::vector<const llvm::Function*> TestCallGraphWrapper::collectAllCallees(const llvm::Function *f){
    auto node = (*cg)[f];
}





char TestCallGraphWrapper::ID = 0;
static llvm::RegisterPass<TestCallGraphWrapper> X("TestCallGraph", "TestCallGraph");



















std::vector<const llvm::Value*> TestCallGraphWrapper::ptsPointsTo(const llvm::Instruction *user, const llvm::Instruction *t){
    std::vector<const llvm::Value*> res;

    auto candidatePointers = pointsToSet[user];
    for(auto iter = candidatePointers.begin(); iter != candidatePointers.end(); ++iter){
        auto pts = iter->second;
        auto it = std::find_if(pts.first.begin(), pts.first.end(), [&](const llvm::Value *pvar) -> bool {return pvar == t;});
        if(it != pts.first.end()){
            res.push_back(iter->first);
        }
    }

    return res;
}




bool operator<(const Label &l1, const Label &l2){
    if(l1.type == l2.type){
        return l1.ptr < l2.ptr;
    }
    else{
        return l1.type < l2.type;
    }
}

llvm::raw_ostream& operator<<(llvm::raw_ostream &os, const Label &l){
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

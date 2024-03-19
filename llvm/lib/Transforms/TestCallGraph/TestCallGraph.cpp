#include "TestCallGraph.h"

#define RED_BOLD_PREFIX "\033[1;31m"
#define GREEN_BOLD_PREFIX "\033[1;32m"
#define YELLOW_BOLD_PREFIX "\033[1;33m"
#define ALL_RESET "\033[0m"


const llvm::Function* TestCallGraphWrapper::getFunctionInCallGrpahByName(std::string name){
    for(auto beg = cg->begin(), end = cg->end(); beg != end; ++beg){
        if(beg->first != nullptr && beg->first->getName() == name){
            return beg->first;
        }
    }
    return nullptr;
}

size_t TestCallGraphWrapper::countPointerLevel(const llvm::Instruction *inst){
    size_t pointerLevel = 1;

    auto ty = inst->getType();
    while(ty->getPointerElementType()->isPointerTy()){
        ++pointerLevel;
        ty = ty->getPointerElementType();
    }
    return pointerLevel;
}

void TestCallGraphWrapper::initialize(const llvm::Function * const func){
    for(auto &inst : llvm::instructions(*func)){
        if(const llvm::AllocaInst *pi = llvm::dyn_cast<llvm::AllocaInst>(&inst)){
            memoryLocationMap[pi] = llvm::MemoryLocation(&inst, llvm::LocationSize(func->getParent()->getDataLayout().getTypeAllocSize(pi->getType())));
            labelMap[pi].insert(Label(&inst, Label::LabelType::Def));
            size_t ptrLvl = countPointerLevel(pi);
            worklist[ptrLvl].insert(&inst);
        }
    }
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
        llvm::errs() << "At program location: " << *inst << ":\n";
        for(auto b = beg->second.begin(), e = beg->second.end(); b != e; ++b){
            auto ptr = b->first;
            auto pointees = b->second;
            for(auto pointee : pointees){
                if(pointee){
                    llvm::errs() << "\t" << *ptr << " ==> " << *pointee << "\n";
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


bool TestCallGraphWrapper::runOnModule(llvm::Module &m){

    getCallGraphFromModule(m);

    auto mainFunctionPtr = getFunctionInCallGrpahByName("main");
    if(mainFunctionPtr){
        llvm::errs() << "Found main function.\n" ;
    }
    else{
        llvm::errs() << "nullptr.\n" ;
    }

    performPointerAnalysisOnFunction(mainFunctionPtr);

    return false;
}


char TestCallGraphWrapper::ID = 0;
static llvm::RegisterPass<TestCallGraphWrapper> X("TestCallGraph", "TestCallGraph");

void TestCallGraphWrapper::performPointerAnalysisOnFunction(const llvm::Function * const func){
    if(visited.count(func)){
        return;
    }

    initialize(func);
    // dumpWorkList();
    size_t currentPointerLevel = worklist.size();
    // llvm::errs() << currentPointerLevel << "\n";

    while(currentPointerLevel != 0){
        propagate(currentPointerLevel, func);
        --currentPointerLevel;
    }

    visited[func] = true;
}

bool TestCallGraphWrapper::hasDef(const llvm::Instruction *pinst, const llvm::Instruction *p){
    auto labels = labelMap[pinst];
    auto iter = std::find_if(labels.begin(), labels.end(), [&](Label l) -> bool {return l.type == Label::LabelType::Def && l.p == p;});
    if(iter != labels.end()){
        return true;
    }
    else{
        return false;
    }
}

std::vector<const llvm::Instruction*> TestCallGraphWrapper::findDefFromBB(const llvm::BasicBlock *pbb, const llvm::Instruction *p){
    auto lastInst = &(pbb->back());
    while(lastInst){
        if(hasDef(lastInst, p)){
            return std::vector<const llvm::Instruction*>{lastInst};
        }
        lastInst = lastInst->getPrevNonDebugInstruction();
    }
    std::vector<const llvm::Instruction*> res;
    for(auto it = pred_begin(pbb); it != pred_end(pbb); ++it){
        const llvm::BasicBlock *pbb = *it;
        std::vector<const llvm::Instruction*> defs = findDefFromBB(pbb, p);
        res.insert(res.end(), defs.begin(), defs.end());
    }
    return res;
}

std::vector<const llvm::Instruction*> TestCallGraphWrapper::findDefFromUse(const llvm::Instruction *ptr, const llvm::Instruction *pi){
    while(true){
        auto tmp = ptr->getPrevNonDebugInstruction();
        if(tmp){
            if(hasDef(tmp, pi)){
                return std::vector<const llvm::Instruction*>{tmp};
            }
            ptr = tmp;
        }
        else{
            std::vector<const llvm::Instruction*> res;
            for(auto it = pred_begin(ptr->getParent()); it != pred_end(ptr->getParent()); ++it){
                const llvm::BasicBlock *pbb = *it;
                std::vector<const llvm::Instruction*> defs = findDefFromBB(pbb, pi);
                res.insert(res.end(), defs.begin(), defs.end());
            }
            return res;
        }
    }
}

std::vector<const llvm::Instruction*> TestCallGraphWrapper::getDUEdgesOfPtrAtClause(std::map<const llvm::Instruction*, std::vector<const llvm::Instruction*>> u2p, const llvm::Instruction *ptr){
    std::vector<const llvm::Instruction*> res;
    for(auto iter = u2p.begin(); iter != u2p.end(); ++iter){
        for(auto it = iter->second.begin(); it != iter->second.end(); ++it){
            if(*it == ptr){
                res.push_back(iter->first);
            }
        }
    }
        
    return res;
}

void TestCallGraphWrapper::propagatePointsToInformation(const llvm::Instruction *t, const llvm::Instruction *f, const llvm::Instruction *pvar){
    pointsToSet[t][pvar].insert(pointsToSet[f][pvar].begin(), pointsToSet[f][pvar].end());
    return;
}

void TestCallGraphWrapper::calculatePointsToInformationForStoreInst(const llvm::Instruction *t, const llvm::Instruction *pvar, const llvm::StoreInst *pt){
    std::set<const llvm::Value*> pointees = aliasMap[t][pt->getValueOperand()];
    if(pointees.empty()){
        pointsToSet[t][pvar] = std::set<const llvm::Value*>{llvm::dyn_cast<llvm::Instruction>(pt->getValueOperand()->stripPointerCasts())};
    }
    else{
        pointsToSet[t][pvar] = pointees;
    }
    
    return;
}


void TestCallGraphWrapper::updateAliasInformation(const llvm::Instruction *t, const llvm::Instruction *pt){
    auto aliases = getAlias(t, pt);

    for(auto &b : aliases){
        auto a = llvm::dyn_cast<llvm::Instruction>(b);
        aliasMap[t][t].insert(pointsToSet[t][a].begin(), pointsToSet[t][a].end());
    }
    
    // llvm::errs() << aliasMap[t][t] << "\n";

    return;
}


std::set<const llvm::Value*> TestCallGraphWrapper::getAlias(const llvm::Instruction *t, const llvm::Instruction *p){
    if(auto pt = llvm::dyn_cast<llvm::StoreInst>(p)){
        std::set<const llvm::Value*> pointees = aliasMap[t][pt->getValueOperand()];
        if(pointees.empty()){
            return std::set<const llvm::Value*>{llvm::dyn_cast<llvm::Instruction>(pt->getValueOperand())};
        }
        return pointees;
    }
    else if(auto pt = llvm::dyn_cast<llvm::LoadInst>(p)){
        std::set<const llvm::Value*> pointees = aliasMap[t][pt->getPointerOperand()];
        if(pointees.empty()){
            return std::set<const llvm::Value*>{llvm::dyn_cast<llvm::Instruction>(pt->getPointerOperand())};
        }
        return pointees;
    }
}

std::vector<const llvm::Value*> TestCallGraphWrapper::ptsPointsTo(const llvm::Instruction *user, const llvm::Instruction *t){
    std::vector<const llvm::Value*> res;

    auto candidatePointers = pointsToSet[user];
    for(auto iter = candidatePointers.begin(); iter != candidatePointers.end(); ++iter){
        auto pointsToSet = iter->second;
        auto it = std::find_if(pointsToSet.begin(), pointsToSet.end(), [&](const llvm::Value *pvar) -> bool {return pvar == t;});
        if(it != pointsToSet.end()){
            res.push_back(iter->first);
        }
    }

    return res;
}



void TestCallGraphWrapper::propagate(size_t currentPtrLvl, const llvm::Function* func){

    for(auto ptr : worklist[currentPtrLvl]){
        llvm::errs() << "\n\nCurrent working pointer: " << *ptr << "\n";
        for(auto puser : ptr->users()){
            auto inst = llvm::dyn_cast<llvm::Instruction>(puser);
            llvm::errs() << "Current user: " << *inst << "\n";
            if(auto *storeInst = llvm::dyn_cast<llvm::StoreInst>(inst)){
                /*
                    For store a b, we know we are defining the points-to set of b and perform strong/weak update based on the current points-to set of b.
                */
                labelMap[inst].insert(Label(storeInst->getPointerOperand(), Label::LabelType::Def));
                labelMap[inst].insert(Label(storeInst->getPointerOperand(), Label::LabelType::Use));
                // labelMap[inst].insert(Label(storeInst->getValueOperand(), Label::LabelType::Alias));
                useList[storeInst->getPointerOperand()].push_back(inst);
            }
            else if(auto *loadInst = llvm::dyn_cast<llvm::LoadInst>(inst)){
                /*
                    For a = load b, we will need to know the points-to set of b
                */
                labelMap[inst].insert(Label(loadInst->getPointerOperand(), Label::LabelType::Use));
                // labelMap[inst].insert(Label(inst, Label::LabelType::AliasDefine));
                useList[loadInst->getPointerOperand()].push_back(inst);
            }
            else{
                llvm::errs() << *inst << " is in the user list of pointer " << *ptr << ", but it's neither storeinst nor loadinst.\n";
            }
        }

        // dumpLabelMap();

        

        for(auto usedPointer : useList[ptr]){
            llvm::errs() << "Finding def for " << *usedPointer << "\n";
            auto pd = findDefFromUse(usedPointer, ptr);

            for(auto def : pd){
                defUseGraph[def][usedPointer].push_back(ptr);
                llvm::errs() << "Add def use edge: " << *def << "=== " << *ptr << " ===>" << *usedPointer << "\n";
            }
        }

        // dumpDefUseGraph();

        auto initialDUEdges = getDUEdgesOfPtrAtClause(defUseGraph[ptr], ptr);

        // for(auto i : initialDUEdges){
        //     llvm::errs() << "edge " << *ptr << " === " << *ptr << " ===> " << *i << "\n";
        // }

        std::vector<std::tuple<const llvm::Instruction*, const llvm::Instruction*, const llvm::Instruction*>> propagateList;
        for(auto pu : initialDUEdges){
            propagateList.push_back(std::make_tuple(ptr, pu, ptr));
        }


        while(!propagateList.empty()){
            auto tup = propagateList.front();
            propagateList.erase(propagateList.begin());
        //     // errs() << propagateList;
            auto f = std::get<0>(tup);
            auto t = std::get<1>(tup);
            auto pvar = std::get<2>(tup);
            llvm::errs() << "Propagating along def use edge: " << *f << " ===== " << *pvar << " ====> " << *t << "\n";
        //     // todo: but we also neeo to consider if we need to make it an assignment to some points to result due to information passed along a single edge.
        //     // todo: one problem is that in the case 3->1 along path A and 3->2 along path 2 and path A is then updated to 3->4, the result 
        //     //      is wrong by {1,2,4} instead of {2,4}.
            propagatePointsToInformation(t, f, pvar);

            if(auto pt = llvm::dyn_cast<llvm::StoreInst>(t)){
                auto tmp = pointsToSet[t][pvar];
                // todo: update all vector to set
                calculatePointsToInformationForStoreInst(t, pvar, pt);
                if(tmp != pointsToSet[t][pvar]){
                    llvm::errs() << "points-to set changed.\n";
                    auto passList = getDUEdgesOfPtrAtClause(defUseGraph[t], pvar);
                    for(auto u : passList){    
                            propagateList.push_back(std::make_tuple(t,u,pvar));
                            llvm::errs() << "New def use edge added to propagatelist: " << *t << "=== " << *pvar << " ===>" << *u << "\n";      

                        // if(std::find(defUseGraph[t][u].begin(), defUseGraph[t][u].end(), pvar) == defUseGraph[t][u].end()){
                        //     propagateList.push_back(std::make_tuple(t,u,pvar));
                        //     llvm::errs() << "New def use edge added: " << *t << "=== " << *pvar << " ===>" << *u << "\n";
                        // }

                    }
                }
            }
            else if(auto pt = llvm::dyn_cast<llvm::LoadInst>(t)){
                auto tmp = aliasMap[t][t];
                updateAliasInformation(t,pt);
                if(tmp != aliasMap[t][t]){
                    for(auto user0 : t->users()){
                        // llvm::errs() << "Handling user " << *user0 << "\n";
                        auto user = llvm::dyn_cast<llvm::Instruction>(user0);
                        // todo: we need another list for the case
                        /*
                            y = load x
                            z = load y
                            store z a

                            Here, the aliasMap for z should be propagate to the store clause, but currenly it is not propagated. 
                        */
                        if(auto pt0 = llvm::dyn_cast<llvm::StoreInst>(user)){
                            if(t == pt0->getPointerOperand()){
                                aliasMap[user][t] = aliasMap[t][t];

                                if(auto pins = llvm::dyn_cast<llvm::Instruction>(pt0->getValueOperand())){
                                    // llvm::errs() << *user << " " << *(pt0->getValueOperand()) << " " << *t << "\n";
                                    // llvm::errs() << aliasMap[user][pt0->getValueOperand()] << "\n";
                                    // llvm::errs() << "ttttt: " << aliasMap[user][t] << "\n";
                                    if(aliasMap[user][pt0->getValueOperand()].empty()){
                                        pointsToSet[user][t] = std::set<const llvm::Value*>{pt0->getValueOperand()};
                                        for(auto tt0 : aliasMap[user][t]){
                                            auto tt = llvm::dyn_cast<llvm::Instruction>(tt0);
                                            pointsToSet[user][tt] = pointsToSet[user][t];
                                        }
                                    }
                                    else{
                                        pointsToSet[user][t] = aliasMap[user][pt0->getValueOperand()];
                                        for(auto tt0 : aliasMap[user][t]){
                                            auto tt = llvm::dyn_cast<llvm::Instruction>(tt0);
                                            pointsToSet[user][tt] = pointsToSet[user][t];
                                        }
                                    }
                                    // pts[user][t] = getAlias(user, dyn_cast<Instruction>(pt0->getValueOperand()));
                                }
                                else{
                                    pointsToSet[user][t] = std::set<const llvm::Value*>{pt0->getValueOperand()};
                                }

                                // llvm::errs() << "Pts ttttt: " << pointsToSet[user][t] << "\n";


                                for(auto tt : aliasMap[user][t]){
                                    // llvm::errs() << "ALARM " << *user << "\n";
                                    labelMap[user].insert(Label(tt, Label::LabelType::Def));
                                    labelMap[user].insert(Label(tt, Label::LabelType::Use));
                                    // auto *inst = llvm::dyn_cast<llvm::Instruction>(tt);
                                    useList[tt].push_back(user);

                                }
                                
                            }
                            else if(t == pt0->getValueOperand()){
                                aliasMap[user][t] = aliasMap[t][t];
                                // todo: are we only need ptspointsto(user,t) or also need ptspointsto(user, aliasmap[user][t])
                                auto ptrChangeList = ptsPointsTo(user,t);
                                // llvm::errs() << "changeList: " << ptrChangeList << "\n";
                                for(auto p0 : ptrChangeList){
                                    // todo: create a function that updates the pts set and return a set of propagation edges.
                                    auto p1 = llvm::dyn_cast<llvm::Instruction>(p0);
                                    pointsToSet[user][p1] = aliasMap[user][t];
                                }
                                // todo: after changing pts set, we need to propagate the change along the def use edge.
                            }
                            else{
                                llvm::errs() << "Hitting at " << *pt0 << " with pointer " << *t << "\n";
                            }
                        }
                        else if(auto pt0 = llvm::dyn_cast<llvm::LoadInst>(user)){
                            aliasMap[user][t] = aliasMap[t][t];
                            for(auto tt : aliasMap[user][t]){
                                labelMap[user].insert(Label(tt, Label::LabelType::Use));
                                useList[tt].push_back(user);
                            }

                        }
                        else{
                            llvm::errs() << "Wrong clause type: " << *user << "\n";
                        }

                        // dumpDebugInfo();
                    }
                }
            }
            // dumpDebugInfo();
            // todo: add dump points-to set.
            dumpLabelMap();
            dumpPointsToMap();
        }

    }
}

bool operator<(const Label &l1, const Label &l2){
    if(l1.type == l2.type){
        return l1.p < l2.p;
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
        os << "Def(" << *l.p << ")";
    }
    else if(l.type == Label::LabelType::Use){
        os << "Use(" << *l.p << ")";
    }
    else if(l.type == Label::LabelType::DefUse){
        os << "DefUse(" << *l.p << ")";
    }
    return os;
}

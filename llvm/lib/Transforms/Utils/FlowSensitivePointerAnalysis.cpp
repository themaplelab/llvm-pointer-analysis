#include "llvm/Transforms/Utils/FlowSensitivePointerAnalysis.h"

#include "llvm/Support/Debug.h"
#include "llvm/Support/Timer.h"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <sstream>

using namespace llvm;

AnalysisKey FlowSensitivePointerAnalysis::Key;

/// @brief Print the points-to set for \p Ptr at program location \p Loc when 
///     running in debug mode. 
void FlowSensitivePointerAnalysis::printPointsToSetAtProgramLocation(const ProgramLocationTy *Loc){

    if(pointsToSet.count(Loc)){
        DEBUG_WITH_TYPE("fspa", dbgs() << "At program location" << *Loc << ":\n");
        for(auto const& [Ptr, PTS] : pointsToSet.at(Loc)){
            DEBUG_WITH_TYPE("fspa", dbgs() << *Ptr << " ==>\n");
            for(auto Pointee : PTS.first){
                if(dyn_cast<Instruction>(Pointee)){
                    DEBUG_WITH_TYPE("fspa", dbgs() << "\t" << *Pointee << "\n");
                }
                else{
                    DEBUG_WITH_TYPE("fspa", dbgs() << "\t " << *Pointee << "\n");
                }
            }
        }
    }

}

static std::string getCurrentTime(){
    const auto now = std::chrono::system_clock::now();
    const std::time_t t_c = std::chrono::system_clock::to_time_t(now);
    auto gmt_time = gmtime(&t_c);
    std::stringstream sstream;
    sstream << std::put_time(gmt_time, "%Y/%m/%d %T");

    return "[" + sstream.str() + "]";
}

void FlowSensitivePointerAnalysis::dumpPointsToSet(){
    dbgs() << "Print points-to set stats\n";
    // C++26 will treat _ as a special value that does not cause unused warning.
    for(auto const& [Loc, _] : pointsToSet){
        printPointsToSetAtProgramLocation(Loc);
    }
}

void FlowSensitivePointerAnalysis::dumpLabelMap(){

    dbgs() << "Print label map\n";
    for(auto p : labelMap){
        dbgs() << "Labels at" << *p.first << "\n";
        for(auto e : p.second){
            dbgs() << "\t" << e << "\n";
        }
    }

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
        
        while(CurrentPointerLevel){
            for(auto &Func : m.functions()){
                processGlobalVariables(CurrentPointerLevel);
                performPointerAnalysisOnFunction(&Func, CurrentPointerLevel);
            }
            --CurrentPointerLevel;

        }

        DEBUG_WITH_TYPE("fspa", dumpLabelMap());
        DEBUG_WITH_TYPE("fspa", dumpPointsToSet());
        DEBUG_WITH_TYPE("fspa", dbgs() << getCurrentTime() << "Populating PTS\n");

        populatePointsToSet(m);

        DEBUG_WITH_TYPE("fspa", dumpPointsToSet());

        result.setFunc2Pointers(Func2AllocatedPointersAndParameterAliases);
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

void FlowSensitivePointerAnalysis::populatePointsToSet(Module &m){
    // new implementation. consult defloc for each pointer at loc. if no defloc, then {}, else grab pts.


    for(auto &Func : m.functions()){
        SetVector<const PointerTy*> AllocatedPointersAndParameterAliases{};
        auto AllocatedPointersInFunc = DenseSet<const PointerTy*>{};
        auto WorkList = std::map<size_t, std::set<const PointerTy *>>{};
        if(func2worklist.count(&Func)){
            WorkList = func2worklist.at(&Func);
        }
        
        for(auto m : WorkList){
            AllocatedPointersAndParameterAliases.insert(m.second.begin(), m.second.end());
            AllocatedPointersInFunc.insert(m.second.begin(), m.second.end());
        }
        
        if(funcParas2AliasSet.count(&Func)){
            auto Para2AliasSet = funcParas2AliasSet[&Func];
            for(auto p : Para2AliasSet){
                AllocatedPointersAndParameterAliases.insert(p.second.begin(), p.second.end());
            }
        }

        Func2AllocatedPointersAndParameterAliases[&Func] = AllocatedPointersAndParameterAliases;
        
        // Assume we are starting with the first instruction of the function.
        auto StartPTS = std::map<const PointerTy *, std::set<const PointerTy *>>{};
        for(auto P : funcParas2AliasSet[&Func]){
            for(auto CLoc : func2CallerLocation[&Func]){
                for(auto alias : P.second){
                    for(auto pe : pointsToSet[CLoc][alias].first){
                        if(!dyn_cast<LoadInst>(pe)){
                            StartPTS[alias].insert(pe);
                        }
                    }
                    // StartPTS[alias].insert(pointsToSet[CLoc][alias].first.begin(), pointsToSet[CLoc][alias].first.end());
                }
                
            }
        }
        auto cur = Func.getEntryBlock().getFirstNonPHIOrDbg();
        DenseSet<const ProgramLocationTy*> Visited{};

        populatePTSAtLocation(cur, StartPTS, Visited);

    }
}

void FlowSensitivePointerAnalysis::populatePTSAtLocation(const ProgramLocationTy *Loc, std::map<const PointerTy *, std::set<const PointerTy *>> PassedPTS, DenseSet<const ProgramLocationTy*> &Visited){
    bool changed = false;


    if(Visited.contains(Loc)){
        return;
    }
    Visited.insert(Loc);

    auto oldPTS = pointsToSet[Loc];

    for(auto P : Func2AllocatedPointersAndParameterAliases[Loc->getFunction()]){
        if(hasDef(Loc, P)){
            for(auto it = pointsToSet[Loc][P].first.begin(); it != pointsToSet[Loc][P].first.end();){
                if(dyn_cast<LoadInst>(*it)){
                    it = pointsToSet[Loc][P].first.erase(it);
                }
                else{
                    ++it;
                }
            }
            
            PassedPTS[P] = pointsToSet[Loc][P].first;  
        }
        else if(PassedPTS.count(P)){
            pointsToSet[Loc][P].first.insert(PassedPTS[P].begin(), PassedPTS[P].end());
        }
    }
    if(oldPTS != pointsToSet[Loc]){
        changed = true;
    }

    auto Next = Loc->getNextNonDebugInstruction();
    if(!Next){
        for(auto NextBB : successors(Loc->getParent())){
            if(changed || !Visited.contains(NextBB->getFirstNonPHIOrDbg())){
                populatePTSAtLocation(NextBB->getFirstNonPHIOrDbg(), PassedPTS, Visited);
            }
            
            
        }
    }
    else{
        if(changed || !Visited.contains(Next)){
            populatePTSAtLocation(Next, PassedPTS, Visited);
        }
    }


}





void FlowSensitivePointerAnalysis::processGlobalVariables(int ptrLvl){
    if(!globalWorkList.count(ptrLvl)){
        return;
    }
    for(auto globalPtr : globalWorkList.at(ptrLvl)){
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
        ++TotalFunctionNumber;
        visited.emplace(&func, false);
        initialize(&func);
        if(ptrLvl < func2worklist.at(&func).size()){
            ptrLvl = func2worklist.at(&func).size();
        }
    }

    assert(ptrLvl >= 0 && "Pointer level cannot be negative.");

    result.setWorkList(func2worklist);
    TotalFunctionNumber *= ptrLvl;
    return ptrLvl;
}

void FlowSensitivePointerAnalysis::performPointerAnalysisOnFunction(const Function *func, size_t ptrLvl){
    DEBUG_WITH_TYPE("fspa", dbgs() << getCurrentTime() << "Analyzing function: " 
        << func->getName() << " with pointer level: " << ptrLvl << "\n");


    visited.at(func) = true;
    auto pointers = std::set<const PointerTy *>{};
    if(func2worklist.count(func) && func2worklist[func].count(ptrLvl)){
        pointers = func2worklist.at(func).at(ptrLvl);
    }
    
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
        if(!visited.at(callee)){
            performPointerAnalysisOnFunction(callee, ptrLvl);
        }
        
    }
    // logger << "1.5" << "\n";

    ++ProcessedFunctionNumber;

}

std::set<const Function*> FlowSensitivePointerAnalysis::getCallees(const Function *func){
    
    if(caller2Callee.count(func)){
        return caller2Callee.at(func);
    }
    return std::set<const Function *>{};
    
}

std::set<const FlowSensitivePointerAnalysis::ProgramLocationTy*> FlowSensitivePointerAnalysis::getUseLocations(const PointerTy *ptr){
    if(useList.count(ptr)){
        return useList.at(ptr);
    }
    return std::set<const ProgramLocationTy *>{};
}

void FlowSensitivePointerAnalysis::markLabelsForPtr(const PointerTy *ptr){

    // logger << "Marking labels for " << *ptr << "\n";


    for(auto user : ptr->users()){
        
        auto inst = dyn_cast<Instruction>(user);
        if(auto *storeInst = dyn_cast<StoreInst>(inst)){
            if(storeInst->getValueOperand() == ptr){
                continue;
            }
            // logger << "Marking def labels for " << *ptr << " at " << *inst << "\n";
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

void FlowSensitivePointerAnalysis::buildDefUseGraph(std::set<const ProgramLocationTy*> useLocs, const PointerTy *ptr){
    for(auto useLoc : useLocs){
        // logger << "Building def-use graph for " << *ptr << " at " << *useLoc << "\n";

        // auto defLocs = FindDefInBasicBlock(useLoc, ptr, visited);
        auto defLocs = FindDefFromPrevOfUseLoc(useLoc, ptr);
        // logger << "Find " << defLocs.size() << " defs.\n";
        for(auto def : defLocs){
            addDefUseEdge(def, useLoc, ptr);
        }
        // logger << "Finish add edge\n";
    }
}

std::vector<FlowSensitivePointerAnalysis::DefUseEdgeTupleTy> FlowSensitivePointerAnalysis::initializePropagateList(std::set<const PointerTy*> pointers, size_t ptrLvl){
    std::vector<DefUseEdgeTupleTy> propagateList;
    for(auto ptr: pointers){
        auto Loc = dyn_cast<Instruction>(ptr);
        assert(Loc && "cannot use nullptr as program location");
        auto initialDUEdges = getAffectUseLocations(Loc, ptr);
        for(auto pu : initialDUEdges){
            propagateList.push_back(std::make_tuple(Loc, pu, ptr));
        }
    }
    if(globalWorkList.count(ptrLvl)){
        for(auto ptr : globalWorkList.at(ptrLvl)){

        }
    }
    

    // logger << "Initialized propagate list for " << pointers.size() << " pointers, it has " << propagateList.size() << " edges.\n";
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

        // logger << "Propagating edge " << *f << " === " << *ptr << " ===> " << *t << "\n";

        propagatePointsToInformation(t, f, ptr);

        if(auto storeInst = dyn_cast<StoreInst>(t)){
            auto pts = getRealPointsToSet(t, storeInst->getValueOperand());
            updatePointsToSet(t, ptr, pts, propagateList);
            updatePointsToSet(t, storeInst->getPointerOperand(), pts, propagateList);


        }
        else if(auto loadInst = dyn_cast<LoadInst>(t)){
            auto tmp = std::set<const Value*>{};
            if(aliasMap.count(t)){
                if(aliasMap[t].count(t)){
                    tmp = aliasMap.at(t).at(t);
                }
            }
            // logger << "Before update (" << *t << ")\n";
            // for(auto e : tmp){
            //     logger << *e << " ";
            // }
            // logger << "\n";
            updateAliasInformation(t,loadInst);
            // logger << "After update (" << *t << ")\n";
            // for(auto e : aliasMap[t][t]){
            //     logger << *e << " ";
            // }
            // logger << "\n";
            // Everytime we update the alias information for pointer pt at location t, we need to add the program locaton t to the users of pt.
            
            auto newAliasSet = std::set<const Value*>{};
            if(aliasMap.count(t)){
                if(aliasMap[t].count(t)){
                    newAliasSet = aliasMap.at(t).at(t);
                }
            }

            if(tmp != newAliasSet){
                if(!aliasUser.count(t)){
                    aliasUser[t] = std::set<const User*>();
                    for(auto user : t->users()){
                        aliasUser[t].insert(user);
                    }
                }
                updateAliasUsers(aliasUser.at(t), t, propagateList);
            }
        }
        propagateList.erase(propagateList.begin());
    }

    return;

}

void FlowSensitivePointerAnalysis::updateAliasUsers(std::set<const User *> users, const ProgramLocationTy *Loc, std::vector<DefUseEdgeTupleTy> &propagateList){
    
    
    for(auto user : users){
        // Passing alias-set(Loc) at Loc to user.

        // logger << "Updating alias user " << *user << " for " << *Loc << "\n";
        
        auto userInst = dyn_cast<Instruction>(user);
        if(aliasMap.count(Loc) && aliasMap[Loc].count(Loc)){
            aliasMap[userInst][Loc] = aliasMap.at(Loc).at(Loc);
        }
        else{
            aliasMap[userInst][Loc] = std::set<const PointerTy*>{};
        }
        

        if(auto storeInst = dyn_cast<StoreInst>(userInst)){
            
            if(Loc == storeInst->getPointerOperand()){ 
                // if user is 'store x y', and we are passing alias-set(y), we need to make
                // pts(z) = pts(y) for each z in alias-set(y)



                for(auto tt : aliasMap.at(userInst).at(Loc)){
                    auto PTS = std::set<const PointerTy *>{};
                    if(pointsToSet.count(userInst) && pointsToSet[userInst].count(Loc)){
                        PTS = pointsToSet.at(userInst).at(Loc).first;
                    }

                    updatePointsToSet(userInst, tt, PTS, propagateList);
                    // logger << "(updateAliasUsers) Marking def labels for " << *tt << " at " << *userInst << "\n";
                    labelMap[userInst].insert(Label(tt, Label::LabelType::Def));
                    labelMap[userInst].insert(Label(tt, Label::LabelType::Use));
                    useList[tt].insert(userInst);

                }
                
            }
            else if(Loc == storeInst->getValueOperand()){
                // logger << "\tFind value operand" << "\n";
                
                // get all pointers that point to loc
                auto ptrChangeList = ptsPointsTo(userInst,Loc);
                // logger << "Found change list\n";
                // for(auto e : ptrChangeList){
                //     logger << *e << " ";
                // }
                // logger << "\n";

                auto aliasSet = aliasMap.at(userInst).at(Loc);
                // logger << "Going to insert alias pointer\n";
                // for(auto e : aliasSet){
                //     logger << *e << " ";
                // }
                // logger << "\n";

                
                for(auto p0 : ptrChangeList){
                    assert(p0 && "Cannot process nullptr");
                    // logger << "\t\tChanging the points-to set for pointer " << *p0 << "\n";

                    auto tmp = std::set<const PointerTy *>{};
                    if(pointsToSet.count(userInst)){
                        if(pointsToSet[userInst].count(p0)){
                            tmp = pointsToSet.at(userInst).at(p0).first;
                        }
                    }
                    
                    
                    // todo: refactor all update to points-to set as a function.                    
                    pointsToSet[userInst][p0].first.insert(aliasSet.begin(), aliasSet.end());

                    // Here, if points2set is changed, we need to propagate.
                    if(tmp != pointsToSet.at(userInst).at(p0).first){
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

            for(auto tt : aliasMap.at(userInst).at(Loc)){
                labelMap[userInst].insert(Label(tt, Label::LabelType::Use));
                useList[tt].insert(userInst);
            }

        }
        else if(auto pt0 = dyn_cast<ReturnInst>(userInst)){

            for(auto tt : aliasMap.at(userInst).at(Loc)){
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

            for(auto tt : aliasMap.at(userInst).at(Loc)){
                labelMap[userInst].insert(Label(tt, Label::LabelType::Use));
                useList[tt].insert(userInst);
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
            

            auto changed = updateArgAliasOfFunc(CallInstruction->getCalledFunction(), aliasMap.at(userInst).at(Loc), ArgumentIdx);
        }
        else{
            #define DEBUG_TYPE "TESTCALLGRAPH"
            LLVM_DEBUG(dbgs() << "Wrong clause type: " << *userInst << "\n");
            #undef DEBUG_TYPE
        }

    }
}

bool FlowSensitivePointerAnalysis::updateArgPtsofFunc(const Function *func, const PointerTy *ptr, std::set<const Value*> pts){
    // Densemap has no at member function in llvm-14. Move back to use std::map.

    auto oldSize = funcParas2PointsToSet.at(func).at(ptr).size();
    funcParas2PointsToSet.at(func).at(ptr).insert(pts.begin(), pts.end());

    return (oldSize != funcParas2PointsToSet.at(func).at(ptr).size());
}

bool FlowSensitivePointerAnalysis::updateArgAliasOfFunc(const Function* func, std::set<const Value *> aliasSet, size_t argIdx){
    
    const Value *para;
    for(auto &pa : func->args()){
        para = &pa;
        if(!argIdx){
            break;
        }
        --argIdx;
    }

    size_t oldSize = 0;
    if(funcParas2AliasSet.count(func) && funcParas2AliasSet[func].count(para)){
        oldSize = funcParas2AliasSet.at(func).at(para).size();
    }
    
    funcParas2AliasSet[func][para].insert(aliasSet.begin(), aliasSet.end());

    // logger << "Updating alias map for paras " << *para << " of function " << func->getName() << "\n";
    // logger << "Alias to ";
    // for(auto a : funcParas2AliasSet[func][para]){
    //     logger << *a << " ";
    // }
    // logger << "\n";

    return (oldSize != funcParas2AliasSet.at(func).at(para).size());
}

/// @brief Perform either strong update or weak update for \p Pointer at \p Loc
///     according to the size of aliases of \p Pointer. Add def-use edges to
///     \p propagateList if needed.
/// @param Loc 
/// @param Pointer 
/// @param AdjustedPointsToSet 
/// @param propagateList 
void FlowSensitivePointerAnalysis::updatePointsToSet(const ProgramLocationTy *Loc, const Value *Pointer, std::set<const Value *> AdjustedPointsToSet, std::vector<DefUseEdgeTupleTy> &propagateList){
    // logger << "Updating point-to-set for " << *Pointer << " at " << *Loc << "\n";

    auto tmp = std::set<const Value *>{};
    if(aliasMap.count(Loc)){
        if(aliasMap[Loc].count(Pointer)){
            tmp = aliasMap.at(Loc).at(Pointer);
        }
    }
    
    
    assert(Pointer && "Cannot process nullptr");
    
    bool changed = false;
    // auto originalPts = pointsToSet.at(Loc).at(Pointer).first;
    if(tmp.size() <= 1){
        changed = updatePointsToSetAtProgramLocation(Loc, Pointer, AdjustedPointsToSet);
    }
    else{
        pointsToSet[Loc][Pointer].first.insert(AdjustedPointsToSet.begin(), AdjustedPointsToSet.end());
        changed = updatePointsToSetAtProgramLocation(Loc, Pointer, pointsToSet[Loc][Pointer].first);
    }

    if(changed){
        // logger << "Original points-to set of " << *Pointer << " at " << *Loc << "\n";
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

/// @brief Update the alias set of pointer x introduced by a \p loadInst "x = load y"
/// @param loc 
/// @param loadInst 
void FlowSensitivePointerAnalysis::updateAliasInformation(const ProgramLocationTy *Loc, const LoadInst *loadInst){
    
    auto aliases = getAlias(Loc, loadInst);
    for(auto &b : aliases){
        // logger << "Alias of operand at " << *loadInst << " : " << *b << "\n";
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

        // logger << "Pts of " << *b << " at " << *Loc << " is\n";
        // for(auto e : pointsToSet.at(Loc).at(b).first){
        //     logger << *e << " ";
        // }
        // logger << "\n";


        aliasMap[Loc][loadInst].insert(pointsToSet.at(Loc).at(b).first.begin(), pointsToSet.at(Loc).at(b).first.end());
    }
    
    return;
}

std::set<const Value*> FlowSensitivePointerAnalysis::getAlias(const ProgramLocationTy *t, const Instruction *p){
    // for a store inst "store a b", we get the alias set of a at t.
    if(auto pt = dyn_cast<StoreInst>(p)){
        auto pointees = std::set<const PointerTy *>{};
        if(aliasMap.count(t)){
            if(aliasMap[t].count(pt->getValueOperand())){
                pointees = aliasMap.at(t).at(pt->getValueOperand());
            }
        }
        if(pointees.empty()){
            return std::set<const Value*>{dyn_cast<Instruction>(pt->getValueOperand())};
        }
        return pointees;
    }
    // for a "a = load b", we get the alias set of b at t.
    else if(auto pt = dyn_cast<LoadInst>(p)){
        auto pointees = std::set<const PointerTy *>{};
        if(aliasMap.count(t)){
            if(aliasMap[t].count(pt->getPointerOperand())){
                pointees = aliasMap.at(t).at(pt->getPointerOperand());
            }
        }
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


/// @brief Get the set of real pointee represented by a pointer. For a storeInst 
///     store x y, x maybe an parameter of a function or a temporary register. In our
///     analysis, we only want to propagate allocated pointer. Return itself iff no
///     allocated pointers are alias to it.
/// @param Loc Program location that we want to query the alias set.
/// @param ValueOperand Value operand of a store instruction. We will find all allocated pointers that
///     alias to it.
/// @return  A set of allocated pointers or \p ValueOperand.
std::set<const Value*> FlowSensitivePointerAnalysis::getRealPointsToSet(const ProgramLocationTy *Loc, const Value *ValueOperand){
    
    std::set<const Value*> pointees{};
    if(dyn_cast<Argument>(ValueOperand)){
        if(funcParas2AliasSet.count(Loc->getFunction())){
            if(funcParas2AliasSet[Loc->getFunction()].count(ValueOperand)){
                pointees =  funcParas2AliasSet.at(Loc->getFunction()).at(ValueOperand);
            }
        }
        
    }
    else{
        if(aliasMap.count(Loc)){
            if(aliasMap[Loc].count(ValueOperand)){
                pointees = aliasMap.at(Loc).at(ValueOperand);
            }
        }
    }

    // logger << "real pts for " << *ValueOperand << " is\n";
    // for(auto e : pointees){
    //     logger << *e << " ";
    // }
    // logger << "\n";
    
    return (pointees.empty() ? std::set<const Value*>{ValueOperand->stripPointerCasts()} : pointees);

}

/// @brief 
/// @param loc 
/// @param ptr 
/// @return 
std::vector<const FlowSensitivePointerAnalysis::ProgramLocationTy*> FlowSensitivePointerAnalysis::getAffectUseLocations(const ProgramLocationTy *loc, const Value *ptr){
    std::vector<const Instruction*> res{};

    std::map<const PointerTy*, std::set<const ProgramLocationTy*>> ToLocAndPtrs{};

    if(defUseGraph.count(loc)){
        ToLocAndPtrs = defUseGraph.at(loc);
    }

    for(auto iter = ToLocAndPtrs.begin(); iter != ToLocAndPtrs.end(); ++iter){
        if(ptr == iter->first){
            res.insert(res.begin(), iter->second.begin(), iter->second.end());
        }
    }

    // logger << "Got " << res.size() << " affect use locations for " << *ptr << " at " << *loc << "\n";
        
    return res;
}

void FlowSensitivePointerAnalysis::propagatePointsToInformation(const ProgramLocationTy *toLoc, const ProgramLocationTy *fromLoc, const PointerTy *var){

    
    // bug: For a store instruction store x y, if we pass pts(y) from fromLoc to
    //      toLoc, it will overwrites existing pts(y) at toLoc. Furthermore, we 
    //      do not really need to pass pts(y) to a store instruction since we are
    //      checking the alias set to perform which kind of points-to set
    //      update. Will remove the use label at store instruction in the future.
    if(dyn_cast<LoadInst>(toLoc) || dyn_cast<CallInst>(toLoc)){
        // logger << "Propagating pts set for " << *var << " from " << *fromLoc << " to " << *toLoc << "\n";
        pointsToSet[toLoc][var].first.insert(pointsToSet.at(fromLoc).at(var).first.begin(), pointsToSet.at(fromLoc).at(var).first.end());
        // logger << "After propagating\n";
        // for(auto e : pointsToSet[toLoc][var].first){
        //     logger << *e << " ";
        // }
        // logger << "\n";
    }

    return;
}

void FlowSensitivePointerAnalysis::addDefUseEdge(const ProgramLocationTy *def, const ProgramLocationTy *use, const PointerTy *ptr){
    logger << "Add def use edge " << *def << " === " << *ptr << " ===> " << *use << "\n";
    defUseGraph[def][ptr].insert(use);
}

std::set<const Instruction*> FlowSensitivePointerAnalysis::FindDefFromPrevOfUseLoc(const ProgramLocationTy *Loc, const PointerTy *Ptr){

    logger << "Finding defs of " << *Ptr << " at program location " << *Loc << "\n";

    if(DefLoc.count(Loc)){
        if(DefLoc.at(Loc).count(Ptr)){
            logger << "Defined\n";
            return DefLoc.at(Loc).at(Ptr);
        }
    }
    

    std::set<const Instruction*> res;
    auto Visited = std::set<const ProgramLocationTy*>();

    std::set<const Instruction *> Prevs{};

    if(dyn_cast<CallInst>(Loc)){
        Prevs = getPrevProgramLocations(Loc, true);
    }
    else{
        Prevs = getPrevProgramLocations(Loc);
    }
    for(auto Prev : Prevs){
        auto defs = FindDefFromUseLoc(Prev, Ptr, Visited);
        res.insert(defs.begin(), defs.end());
    }

    DefLoc[Loc][Ptr] = res;
    return res;

}

std::set<const Instruction*> FlowSensitivePointerAnalysis::FindDefFromUseLoc(const ProgramLocationTy *Loc, const PointerTy *Ptr, std::set<const ProgramLocationTy*> &Visited){

    if(Visited.count(Loc)){
        return std::set<const Instruction*>{};
    }
    Visited.insert(Loc);
    // logger << "Finding defs of " << *Ptr << " at program location " << *Loc << "\n";

    if(hasDef(Loc, Ptr)){
        return std::set<const ProgramLocationTy*>{Loc};
    }

    // Be careful not to implicit create new entry when finding existence.
    if(DefLoc.count(Loc)){
        if(DefLoc.at(Loc).count(Ptr)){
            return DefLoc.at(Loc).at(Ptr);
        }
    }


    std::set<const Instruction*> res;
    
    auto Prevs = getPrevProgramLocations(Loc);
    for(auto Prev : Prevs){
        auto defs = FindDefFromUseLoc(Prev, Ptr, Visited);
        res.insert(defs.begin(), defs.end());
    }
    return res;
    
}


std::set<const Instruction*> FlowSensitivePointerAnalysis::getPrevProgramLocations(const ProgramLocationTy *Loc, bool skip){

    

    std::set<const Instruction*> res{};

    if(auto callInst = dyn_cast<CallInst>(Loc)){
        auto Func = callInst->getCalledFunction();
        if(Func && !skip){
            // If not indirect call
            auto terminatedBBs = std::set<const BasicBlock*>{};
            if(func2TerminateBBs.count(Func)){
                terminatedBBs = func2TerminateBBs.at(Func);
            }
            
            for(auto it = terminatedBBs.begin(), end = terminatedBBs.end(); it != end; ++it){
                assert(*it && "Cannot handle nullptr");
                res.insert(&((*it)->back()));
            }
            return res;
        }
    }

    auto Prev = Loc->getPrevNonDebugInstruction();
    if(Prev){
        return std::set<const Instruction*>{Prev};
    }
    else{
        auto PrevBasicBlocksRange = predecessors(Loc->getParent());
            
        if(PrevBasicBlocksRange.empty()){
            auto CallerLocations = std::set<const Instruction *>{};
            if(func2CallerLocation.count(Loc->getParent()->getParent())){
                CallerLocations = func2CallerLocation.at(Loc->getParent()->getParent());
            }

            for(auto callLoc : CallerLocations){
                auto P = getPrevProgramLocations(callLoc, true);
                res.insert(P.begin(), P.end());
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


/// @brief Find all definition locations in current basicblock starting from a program location. 
///        LLVM has some intrinsic functions for mapping between LLVM program objects and the source-level objects. 
///        These debug instructions are not related to our analysis.
/// @param loc Program location that use \p ptr
/// @param ptr pointer that being used
/// @return A set of definition locations
std::set<const Instruction*> FlowSensitivePointerAnalysis::FindDefInBasicBlock(const ProgramLocationTy *loc, const PointerTy *ptr, std::set<const BasicBlock*> &visited){

    if(visited.count(loc->getParent())){
        return std::set<const Instruction*>();
    }

    // logger << "Finding defs of " << *ptr << " at program location " << *loc << "\n";
    visited.insert(loc->getParent());

    if(auto callInst = dyn_cast<CallInst>(loc)){
        std::set<const Instruction *> defLocs;
        if(callInst->getCalledFunction()){
            defLocs = findDefFromFunc(callInst->getCalledFunction(), ptr, visited);
        }
        return defLocs;
    }

    std::set<const ProgramLocationTy*> res;
    while(true){
        auto prevLoc = loc->getPrevNonDebugInstruction();
        if(prevLoc){
            if(auto callInst = dyn_cast<CallInst>(prevLoc)){
                std::set<const Instruction *> defLocs;
                if(callInst->getCalledFunction()){
                    defLocs = findDefFromFunc(callInst->getCalledFunction(), ptr, visited);
                }
                return defLocs;
            }
            if(hasDef(prevLoc, ptr)){
                return std::set<const Instruction*>{prevLoc};
            }
            loc = prevLoc;
        }
        else{
            std::set<const ProgramLocationTy*> res;
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
std::set<const Instruction*> FlowSensitivePointerAnalysis::findDefFromFunc(const Function *func, const PointerTy *ptr, std::set<const BasicBlock*> &visited){
    // logger << "Finding defs of " << *ptr << " in function " << func->getName() << "\n";
    std::set<const ProgramLocationTy*> res;

    // bug: if func is not an entry if func2TerminateBBs, 
    auto terminatedBBs = func2TerminateBBs.at(func);
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

std::set<const Instruction*> FlowSensitivePointerAnalysis::findDefFromBB(const BasicBlock *bb, const PointerTy *p, std::set<const BasicBlock*> &visited){
    
    // logger << "Finding defs of " << *p << " in basic block " << bb->getName() << "\n";
    if(visited.count(bb)){
        return std::set<const Instruction*>();
    }
    visited.insert(bb);
    
    auto lastInst = &(bb->back());

    while(lastInst){
        if(auto callInst = dyn_cast<CallInst>(lastInst)){
            std::set<const llvm::Instruction *> defLocs;
            if(callInst->getCalledFunction()){
                defLocs = findDefFromFunc(callInst->getCalledFunction(), p, visited);
            }
            return defLocs;
        }

        if(hasDef(lastInst, p)){
            return std::set<const Instruction*>{lastInst};
        }
        lastInst = lastInst->getPrevNonDebugInstruction();
    }
    std::set<const Instruction*> res;
    
    for(auto it = pred_begin(bb); it != pred_end(bb); ++it){
        if(visited.find(*it) == visited.end()){
            std::set<const Instruction*> defs = findDefFromBB(*it, p, visited);
            res.insert(defs.begin(), defs.end());
        }

    }

    if(res.empty()){
        for(auto callLoc : func2CallerLocation.at(bb->getParent())){
            auto defs = FindDefInBasicBlock(callLoc, p, visited);
            res.insert(defs.begin(), defs.end());
        }
    }


    return res;
}

bool FlowSensitivePointerAnalysis::hasDef(const ProgramLocationTy *loc, const PointerTy *ptr){
    // outs() << "HasDef At" << *loc << " with ptr" << *ptr << "\n";
    if(!labelMap.count(loc)){
        return false;
    }
    auto iter = std::find_if(labelMap.at(loc).begin(), labelMap.at(loc).end(), [&](Label l) -> bool {
        return l.type == Label::LabelType::Def && l.ptr == ptr;
        });
    // outs() << "End\n";
    return (iter == labelMap.at(loc).end() ? false : true);
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

    std::map<size_t, std::set<const PointerTy *>> worklist;


    for(auto &inst : instructions(*func)){

        if(const AllocaInst *alloca = dyn_cast<AllocaInst>(&inst)){
            auto PointerLevel = computePointerLevel(&inst);
            worklist[PointerLevel].insert(&inst);
            labelMap[alloca].insert(Label(&inst, Label::LabelType::Def));
            // Empty points-to set means the pointer is undefined.
            pointsToSet[&inst][&inst] = {std::set<const Value*>(), false};
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


    func2worklist.emplace(func, worklist);
    
    return;
}


/// @brief Update points-to-set for \p Ptr at program location \p Loc.
/// @param Loc 
/// @param Ptr 
/// @param PTS 
/// @return True if the points-to set is changed.
bool FlowSensitivePointerAnalysis::updatePointsToSetAtProgramLocation(const ProgramLocationTy *Loc, const PointerTy *Ptr, std::set<const Value*> PTS){
    auto OldPTS = std::set<const Value*>{};
    if(pointsToSet.count(Loc)){
        if(pointsToSet[Loc].count(Ptr)){
            OldPTS = pointsToSet.at(Loc).at(Ptr).first;
        }
    }
    
    if(OldPTS != PTS){
        pointsToSet[Loc][Ptr].first = PTS;
        return true;
    }
    return false;
}



std::vector<const Value*> FlowSensitivePointerAnalysis::ptsPointsTo(const Instruction *user, const Instruction *t){
    std::vector<const Value*> res;

    auto candidatePointers = std::map<const Value *, std::pair<std::set<const PointerTy*>, bool>>{};
    if(pointsToSet.count(user)){
        candidatePointers = pointsToSet.at(user);
    }
    
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

    raw_ostream& operator<<(raw_ostream &os, const std::map<size_t, std::set<const Instruction *>> &wl){
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


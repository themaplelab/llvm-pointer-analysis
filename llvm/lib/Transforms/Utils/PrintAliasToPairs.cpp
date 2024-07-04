#include "llvm/Transforms/Utils/PrintAliasToPairs.h"
#include <algorithm>
#include <iterator>

using namespace llvm;


PreservedAnalyses PrintAliasToPairs::run(Module &m, ModuleAnalysisManager &mam){
    auto result = mam.getResult<FlowSensitivePointerAnalysis>(m);
    auto pts = result.getPointsToSet();
    auto worklist = result.getWorkList();
    auto Func2AllocatedPointersAndParameterAliases = result.getFunc2Pointers();
    

    for(auto &func : m.functions()){
        auto Pointers = SetVector<const Value*>();
        if(Func2AllocatedPointersAndParameterAliases.count(&func)){
            Pointers = Func2AllocatedPointersAndParameterAliases[&func];
        }
        
        processAliasPairsForFunc(&func, Pointers, pts);
    }

    // outs() << "Print points-to set stats\n";
    // for(auto pair : pts){
    //     auto loc = pair.first;
    //     auto pts = pair.second;

    //     outs() << "At " << *loc << "\n";
    //     for(auto p : pts){
    //         outs() << *(p.first) << " c=>:\n";
    //         for(auto e : p.second.first){
    //             outs() << "\t" << *e << " ";
    //         }
    //         outs() << "\n";
    //     }
    // }

    return PreservedAnalyses::all();

}

void PrintAliasToPairs::processAliasPairsForFunc(const Function *func, SetVector<const Value *> Pointers,
                                                std::map<const Instruction *, std::map<const Value *, std::pair<std::set<const Value *>, bool>>> &pts){

    for(auto &Inst : instructions(*func)){
        // outs() << "Inst is " << Inst << Pointers.size() << "\n";
        getAliasResult(&Inst, pts, Pointers);
    }

                                            

    // DenseSet<const Instruction*> Visited{};


    // auto &firstBB = func->getEntryBlock();

    // std::stack<const BasicBlock*> bbs;
    // bbs.push(&firstBB);

    // while(!bbs.empty()){
    //     auto curBB = bbs.top();
    //     bbs.pop();
    //     auto cur = curBB->getFirstNonPHIOrDbg();
    //     while(cur && !Visited.contains(cur)){
    //         Visited.insert(cur);

    //         getAliasResult(cur, pts, Pointers);
    //         if(cur->isTerminator()){
    //             break;
    //         }
    //         cur = cur->getNextNonDebugInstruction();
    //     }

    //     auto numSucc = cur->getNumSuccessors();
    //     int i = 0;
    //     while(i != numSucc){
    //         bbs.push(cur->getSuccessor(i++));
    //     }
    // }
}



void PrintAliasToPairs::getAliasResult(const Instruction *cur, std::map<const Instruction*, std::map<const Value*, std::pair<std::set<const Value*>, bool>>> &pts, SetVector<const Value*> pointers){

    // outs() << "getaliasresults\n";
    // for(auto ptr : pointers){
    //     if(!pts[cur].count(ptr)){
    //         pts[cur][ptr].first = trackPointsToSet(cur, ptr, pts);
    //     }
    // }

    outs() << "At program location: " << *cur << "\n";

    // bug : underflow when pointers.size() == 0
    if(pointers.size() == 0){
        return;
    }
    for(int i = 0; i < pointers.size() - 1; ++i){
        for(int j = i+1; j <= pointers.size() - 1; ++j){
            // outs() << i << " " << j << " " << pointers.size() - 1 << "\n";

            auto pts_i = pts[cur][pointers[i]].first;
            auto pts_j = pts[cur][pointers[j]].first;

            if((pts_i == pts_j) && (pts_i.size() == 1)){
                outs() << *pointers[i] << " MUST ALIAS " << *pointers[j] <<"\n";
                
            }
            else{
                std::set<const Value *> intersec;
                std::set_intersection(pts_i.begin(), pts_i.end(), pts_j.begin(), pts_j.end(), std::inserter(intersec, intersec.begin()));
                if(!intersec.empty()){
                    outs() << *pointers[i] << " MAY ALIAS " << *pointers[j] <<"\n";
                }
                else{
                    outs() << *pointers[i] << " NO ALIAS " << *pointers[j] <<"\n";
                }
            }
        }
    }
    outs() << "\n";

}

/// @brief Compute points-to-set for \p ptr at program location \cur
/// @param cur 
/// @param ptr 
/// @param pts 
/// @return 
std::set<const Value *> PrintAliasToPairs::trackPointsToSet(const Instruction *cur, const Value *ptr, std::map<const Instruction *, std::map<const Value*, std::pair<std::set<const Value *>, bool>>> &pts){
    
    // outs() << "Finding pts for " << *ptr << " at " << *cur << "\n";

    std::set<const Value *> res;

    auto prev = cur->getPrevNonDebugInstruction();
    if(!prev){

        auto bb = cur->getParent();

        for(auto it = pred_begin(bb), end = pred_end(bb); it != end; ++it){
            auto lastInst = &(*it)->back();
            auto res0 = trackPointsToSet(lastInst, ptr, pts);
            res.insert(res0.begin(), res0.end());
        }
    }
    else{
        if(!pts[prev][ptr].first.empty()){
            auto temp = pts[prev][ptr].first;
            for(auto e : temp){
                if(dyn_cast<LoadInst>(e)){
                    continue;
                }
                res.insert(e);
            }

        }
        else{
            res = trackPointsToSet(prev, ptr, pts);
        }
    }

    return res;

}
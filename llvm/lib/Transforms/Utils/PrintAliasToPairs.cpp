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

    return PreservedAnalyses::all();

}

void PrintAliasToPairs::processAliasPairsForFunc(const Function *func, SetVector<const Value *> Pointers,
                                                std::map<const Instruction *, std::map<const Value *, std::set<const Value *>>> &pts){

    for(auto &Inst : instructions(*func)){
        // dbgs() << "Inst is " << Inst << Pointers.size() << "\n";
        getAliasResult(&Inst, pts, Pointers);
    }

                                            
}



void PrintAliasToPairs::getAliasResult(const Instruction *cur, std::map<const Instruction*, std::map<const Value*, std::set<const Value*>>> &pts, SetVector<const Value*> pointers){

    // dbgs() << "getaliasresults\n";
    // for(auto ptr : pointers){
    //     if(!pts[cur].count(ptr)){
    //         pts[cur][ptr].first = trackPointsToSet(cur, ptr, pts);
    //     }
    // }

    dbgs() << "At program location: " << *cur << "\n";

    // bug : underflow when pointers.size() == 0
    if(pointers.size() == 0){
        return;
    }
    for(size_t i = 0; i < pointers.size() - 1; ++i){
        for(size_t j = i+1; j <= pointers.size() - 1; ++j){
            // dbgs() << i << " " << j << " " << pointers.size() - 1 << "\n";

            auto pts_i = pts[cur][pointers[i]];
            auto pts_j = pts[cur][pointers[j]];

            if((pts_i == pts_j) && (pts_i.size() == 1)){
                if(*(pts_i.begin()) == nullptr){
                    dbgs() << *pointers[i] << " NO ALIAS " << *pointers[j] <<"\n";
                }
                else{
                    dbgs() << *pointers[i] << " MUST ALIAS " << *pointers[j] <<"\n";
                }
            }
            else{
                std::set<const Value *> intersec;
                std::set_intersection(pts_i.begin(), pts_i.end(), pts_j.begin(), pts_j.end(), std::inserter(intersec, intersec.begin()));
                if(!intersec.empty()){
                    dbgs() << *pointers[i] << " MAY ALIAS " << *pointers[j] <<"\n";
                }
                else{
                    dbgs() << *pointers[i] << " NO ALIAS " << *pointers[j] <<"\n";
                }
            }
        }
    }
    dbgs() << "\n";

}

/// @brief Compute points-to-set for \p ptr at program location \cur
/// @param cur 
/// @param ptr 
/// @param pts 
/// @return 
std::set<const Value *> PrintAliasToPairs::trackPointsToSet(const Instruction *cur, const Value *ptr, std::map<const Instruction *, std::map<const Value*, std::set<const Value *>>> &pts){
    
    // dbgs() << "Finding pts for " << *ptr << " at " << *cur << "\n";

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
        if(!pts[prev][ptr].empty()){
            auto temp = pts[prev][ptr];
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
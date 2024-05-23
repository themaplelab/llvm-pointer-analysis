#include "llvm/Transforms/Utils/PrintPointsToSet.h"
#include <stack>

using namespace llvm;

PreservedAnalyses PrintPointsToSet::run(Module &m, ModuleAnalysisManager &mam){
    auto result = mam.getResult<FlowSensitivePointerAnalysis>(m);
    auto pts = result.getPointsToSet();
    auto worklist = result.getWorkList();

    // outs() << "size: " << pts.size() << "\n";
    // for(auto pair : pts){
    //     outs() << "At " << *pair.first << " " << pair.first << ": \n";
    //     for(auto p : pair.second){
    //         for(auto e : p.second.first){
    //             outs() << "\t" << *p.first << " => " << *e << "\n";
    //         }
    //     }
    // }

    std::vector<const AllocaInst*> allocatedPointers;
    for(auto pair : worklist){
        for(auto pointer : pair.second){
            if(auto allocaInst = dyn_cast<AllocaInst>(pointer)){
                allocatedPointers.push_back(allocaInst);
            }
        }
    }

    auto cg = CallGraph(m);
    auto res = std::find_if(cg.begin(), cg.end(), [](std::pair<const Function *const, std::unique_ptr<CallGraphNode>> &p) -> bool {
        return p.first && p.first->getName() == "main";
        });

    if(res == cg.end()){
        outs() << "No main function.\n";
        return PreservedAnalyses::all();
    }
    auto ptrToMain = res->first;
    auto firstBB = &ptrToMain->getEntryBlock();

    std::stack<const BasicBlock*> bbs;
    bbs.push(firstBB);

    while(!bbs.empty()){
        auto curBB = bbs.top();
        bbs.pop();
        auto cur = curBB->getFirstNonPHIOrDbg();
        while(cur){
            printPointsToSet(cur, result.getPointsToSet(), allocatedPointers);
            if(cur->isTerminator()){
                // outs() << "Terminator: " << *cur << "\n";
                break;
            }
            cur = cur->getNextNonDebugInstruction();
        }

        auto numSucc = cur->getNumSuccessors();
        int i = 0;
        while(i != numSucc){
            bbs.push(cur->getSuccessor(i++));
        }

        
        
    }

    return PreservedAnalyses::all();
}

void PrintPointsToSet::printPointsToSet(const Instruction *cur, std::map<const Instruction *, std::map<const Instruction *, std::pair<std::set<const Value *>, bool>>> pts, std::vector<const AllocaInst*> pointers){
    
    // outs() << "Print At: "<< *cur << "\n";
    auto curPointsToSet = pts[cur];

    for(auto ptr : pointers){
        if(!curPointsToSet.count(ptr)){
            // outs() << "Size: "<< curPointsToSet.size() << "\n";

            curPointsToSet[ptr].first = trackPointsToSet(cur, ptr, pts);
            // for(auto e : curPointsToSet[ptr].first){
            //     outs() << *e << "\n";
            // }
            // outs() << "Size: "<< curPointsToSet.size() << "\n";

            // outs() << "Size track: "<< curPointsToSet[ptr].first.size() << "\n";

        }
    }

    outs() << *cur << "\n";
    for(auto pair : curPointsToSet){
        outs() << *(pair.first) << " => {";
        // todo: one more comma
        for(auto pointee : pair.second.first){
            outs() << *pointee << ", ";
        }

        outs() << "}\n";
    }
    outs() << "\n";
}

/// @brief  Trace up from \p cur to find all pointees for pointer \p ptr.
/// @param cur 
/// @param ptr 
/// @return 
std::set<const Value *> PrintPointsToSet::trackPointsToSet(const Instruction *cur, const Instruction *ptr, std::map<const Instruction *, std::map<const Instruction *, std::pair<std::set<const Value *>, bool>>> pts){
    
    std::set<const Value *> res;

    auto prev = cur->getPrevNonDebugInstruction();
    if(!prev){

        auto bb = cur->getParent();
        // errs() << "1\n";

        for(auto it = pred_begin(bb), end = pred_end(bb); it != end; ++it){
            auto lastInst = (*it)->getTerminator();
            auto res0 = trackPointsToSet(lastInst, ptr, pts);
            res.insert(res0.begin(), res0.end());
        }
    }
    else{
        // errs() << "2\n";
        if(!pts[prev][ptr].first.empty()){
            // outs() << "Found" << *ptr << " at " << *prev << "\n";
            auto temp = pts[prev][ptr].first;
            for(auto e : temp){
                if(dyn_cast<LoadInst>(e)){
                    continue;
                }
                res.insert(e);
            }

            // res.insert(temp.begin(), temp.end());
        }
        else{
            res = trackPointsToSet(prev, ptr, pts);
        }
    }

    return res;

}
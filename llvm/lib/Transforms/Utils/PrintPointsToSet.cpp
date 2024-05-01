#include "llvm/Transforms/Utils/PrintPointsToSet.h"
#include <stack>

using namespace llvm;

PreservedAnalyses PrintPointsToSet::run(Module &m, ModuleAnalysisManager &mam){
    auto result = mam.getResult<FlowSensitivePointerAnalysis>(m);
    auto pts = result.getPointsToSet();
    auto worklist = result.getWorkList();

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
        errs() << "No main function.\n";
        return PreservedAnalyses::all();
    }
    auto ptrToMain = res->first;
    auto firstBB = &ptrToMain->getEntryBlock();

    std::stack<const BasicBlock*> bbs;
    bbs.push(firstBB);

    while(!bbs.empty()){
        auto curBB = bbs.top();
        auto cur = curBB->getFirstNonPHIOrDbg();
        while(cur){
            printPointsToSet(cur, result.getPointsToSet(), allocatedPointers);
            cur = cur->getNextNonDebugInstruction();
        }

        // errs() << "Processing" << *cur << "\n";
        if(cur->isTerminator()){
            auto numSucc = cur->getNumSuccessors();
            int i = 0;
            while(i != numSucc){
                bbs.push(cur->getSuccessor(i++));
            }
        }
        
        bbs.pop();
    }

    return PreservedAnalyses::all();
}

void PrintPointsToSet::printPointsToSet(const Instruction *cur, std::map<const Instruction *, std::map<const Instruction *, std::pair<std::set<const Value *>, bool>>> pts, std::vector<const AllocaInst*> pointers){
    auto curPointsToSet = pts[cur];

    for(auto ptr : pointers){
        if(!curPointsToSet.count(ptr)){
            curPointsToSet[ptr].first = trackPointsToSet(cur, ptr);
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
    
}

/// @brief  Trace up from \p cur to find all pointees for pointer \p ptr.
/// @param cur 
/// @param ptr 
/// @return 
std::set<const Value *> PrintPointsToSet::trackPointsToSet(const Instruction *cur, const Instruction *ptr){
    // errs() << "At" << *cur << ", searching for " << *ptr << "\n";
    std::set<const Value *> pts;

    auto prev = cur->getPrevNonDebugInstruction();
    if(!prev){

        auto bb = cur->getParent();
        // errs() << "1\n";

        for(auto it = pred_begin(bb), end = pred_end(bb); it != end; ++it){
            auto lastInst = (*it)->getTerminator();
            auto res = trackPointsToSet(lastInst, ptr);
            pts.insert(res.begin(), res.end());
        }
    }
    else{
        // errs() << "2\n";

        pts = trackPointsToSet(prev, ptr);
    }

    return pts;

}
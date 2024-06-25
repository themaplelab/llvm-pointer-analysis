#include "llvm/Transforms/Utils/PointerStats.h"
#include "llvm/IR/Instructions.h"

using namespace llvm;


PreservedAnalyses PointerStats::run(Module &m, ModuleAnalysisManager &mam){

    size_t globalPtrCount = m.global_size();

    size_t inFuncPtrCount = 0;
    size_t ptrRelateInstsCount = 0;
    auto &funcList = m.getFunctionList();
    for(auto &f : funcList){
        for(auto &bb : f){
            for(auto &instruction : bb){
                if(dyn_cast<AllocaInst>(&instruction)){
                    ++inFuncPtrCount;
                }
                if(dyn_cast<LoadInst>(&instruction) || dyn_cast<StoreInst>(&instruction)){
                    ++ptrRelateInstsCount;
                }
            }
        }
    }

    outs() << "Number of Global Pointers : " << globalPtrCount << "\n";
    outs() << "Number of in function pointers : " << inFuncPtrCount << "\n";
    outs() << "Number of pointer related instructions : " << ptrRelateInstsCount << "\n";

    return PreservedAnalyses::all();

}

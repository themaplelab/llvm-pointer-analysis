#include "llvm/Transforms/Utils/PointerStats.h"
#include "llvm/IR/Instructions.h"

#include <algorithm>

using namespace llvm;


PreservedAnalyses PointerStats::run(Module &m, ModuleAnalysisManager &mam){

    size_t globalPtrCount = m.global_size();

    size_t inFuncPtrCount = 0;
    size_t ptrRelateInstsCount = 0;
    size_t numInsts = 0;
    size_t numFuncs = 0;
    std::vector<size_t> FuncSizes;

    auto &funcList = m.getFunctionList();
    for(auto &f : funcList){
        if(f.isDeclaration()){
            continue;
        }
        size_t FuncInsts = 0;
        ++numFuncs;
        for(auto &bb : f){
            for(auto &instruction : bb){
                ++numInsts;
                ++FuncInsts;
                if(auto Alloca = dyn_cast<AllocaInst>(&instruction)){
                    if(Alloca->getType()->getPointerElementType()->isPointerTy()){
                        ++inFuncPtrCount;
                    }
                    
                }
                if(dyn_cast<LoadInst>(&instruction) || dyn_cast<StoreInst>(&instruction)){
                    ++ptrRelateInstsCount;
                }
            }
        }
        FuncSizes.push_back(FuncInsts);
    }

    std::sort(FuncSizes.begin(), FuncSizes.end());


    outs() << "Number of Global Pointers : " << globalPtrCount << "\n";
    outs() << "Number of in function pointers : " << inFuncPtrCount << "\n";
    outs() << "Number of pointer related instructions : " << ptrRelateInstsCount << "\n";
    outs() << "Number of avg func size : " << (double)numInsts / numFuncs << "\n";
    outs() << "Median func size : " << FuncSizes[FuncSizes.size()/2] << "\n";



    return PreservedAnalyses::all();

}

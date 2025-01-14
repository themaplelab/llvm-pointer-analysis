#include "llvm/Transforms/Utils/GenerateDataLogFacts.h"

using namespace llvm;

PreservedAnalyses GenerateDataLogFacts::run(Module &M, ModuleAnalysisManager &MAM){

    int instcnt = 0;
    int funccnt = 0;

    for(const Function &F: M){
        func2id[&F] = funccnt++;
        for(const BasicBlock &BB : F){
            for(const Instruction &I : BB){
                inst2id[&I] = instcnt++;
                if(auto Alloca = dyn_cast<AllocaInst>(&I)){
                    // %a = alloca i8*
                    // create memoryobjects for a and stack variable
                    // add them to map
                    // record Alloc relation "Alloc(instID(I), moid(a), moid(stackMemObj))"

                    auto lhs = MemoryObject(&I);
                    auto rhs = MemoryObject(&I);

                    ID2memoryObject[lhs.getId()] = lhs;
                    ID2memoryObject[rhs.getId()] = rhs;
                    Allocs.push_back({lhs.getId(), rhs.getId()});

                }
            }
        }
    }

    for(auto p : Allocs){
        outs() << "Alloc(" << inst2id[ID2memoryObject[p.first].getInst()] << ", " << p.first << ", " << p.second << ", " << func2id[ID2memoryObject[p.first].getInst()->getFunction()] << ")\n";
    }



    return PreservedAnalyses::all();

}
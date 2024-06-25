#ifndef LLVM_TRANSFORM_POINTER_STATS_H
#define LLVM_TRANSFORM_POINTER_STATS_H

#include "llvm/IR/PassManager.h"

namespace llvm{
    class PointerStats : public PassInfoMixin<PointerStats>{
        public:
            PreservedAnalyses run(Module &m, ModuleAnalysisManager &mam);
    };
}





#endif
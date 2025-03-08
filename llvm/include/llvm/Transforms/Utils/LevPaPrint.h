#ifndef LLVM_TRANSFORM_UTILS_LEVEL_BY_LEVEL_POINTER_ANALYSIS_PRINT_H
#define LLVM_TRANSFORM_UTILS_LEVEL_BY_LEVEL_POINTER_ANALYSIS_PRINT_H

#include "llvm/IR/PassManager.h"
#include "llvm/Transforms/Utils/LevPA.h"

namespace llvm{
    class LevPaPrint : public PassInfoMixin<LevPaPrint>{
        public:
            PreservedAnalyses run(Module &m, ModuleAnalysisManager &mam);
    };
}




#endif
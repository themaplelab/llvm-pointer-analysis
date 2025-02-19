#ifndef LLVM_TRANSFORM_UTILS_POINTER_ANALYSIS_H
#define LLVM_TRANSFORM_UTILS_POINTER_ANALYSIS_H

#include "llvm/IR/PassManager.h"

namespace llvm{
    class PointerAnalysis : public PassInfoMixin<PointerAnalysis>{
        public:
            PreservedAnalyses run(Module &m, ModuleAnalysisManager &mam);
    };
}

#endif
#ifndef LLVM_TRANSFORM_FLOW_SENSITIVE_POINTER_ANALYSIS_PRINT_H
#define LLVM_TRANSFORM_FLOW_SENSITIVE_POINTER_ANALYSIS_PRINT_H

#include "llvm/IR/PassManager.h"
#include "llvm/Transforms/Utils/FlowSensitivePointerAnalysis.h"


namespace llvm{
    class LfspaPrint : public PassInfoMixin<LfspaPrint>{
        public:
            PreservedAnalyses run(Module &m, ModuleAnalysisManager &mam);
    };
}



#endif

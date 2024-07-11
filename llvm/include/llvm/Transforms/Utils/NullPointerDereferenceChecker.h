#ifndef LLVM_TRANSFORMS_UTIL_NULL_POINTER_DEREFERENCE_CHECKER_H
#define LLVM_TRANSFORMS_UTIL_NULL_POINTER_DEREFERENCE_CHECKER_H

#include "llvm/IR/PassManager.h"
#include "llvm/Transforms/Utils/FlowSensitivePointerAnalysis.h"



namespace llvm{

    class NullPointerChecker : public AnalysisInfoMixin<NullPointerChecker>{
        public:
            PreservedAnalyses run(Module &, ModuleAnalysisManager &);
            void CheckNullPtrAtFunc(const Function*, const SetVector<const Value*>&, 
                std::map<const Instruction *, std::map<const Value*, std::set<const Value*>>> &);


    };
} // namespace llvm




#endif
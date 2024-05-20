#ifndef LLVM_TRANSFORMS_UTIL_FUNCTIONCOUNTER_H
#define LLVM_TRANSFORMS_UTIL_FUNCTIONCOUNTER_H

#include "llvm/IR/PassManager.h"

namespace llvm{

    class FunctionCounter : public PassInfoMixin<FunctionCounter>{
        public:
            PreservedAnalyses run(Function &f, FunctionAnalysisManager &fam);

        private:
            size_t counter = 0;
    };

}






#endif



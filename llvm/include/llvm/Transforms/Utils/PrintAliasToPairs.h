#ifndef LLVM_TRANSFORM_UTILS_PRINT_ALIAS_TO_PAIRS_H
#define LLVM_TRANSFORM_UTILS_PRINT_ALIAS_TO_PAIRS_H

#include "llvm/IR/PassManager.h"
#include "llvm/Transforms/Utils/FlowSensitivePointerAnalysis.h"


namespace llvm{
    class PrintAliasToPairs : public PassInfoMixin<PrintAliasToPairs>{
        public:
            PreservedAnalyses run(Module &m, ModuleAnalysisManager &mam);
            void getAliasResult(const Instruction*, std::map<const Instruction *, std::map<const Instruction *, std::pair<std::set<const Value *>, bool>>> pts, std::vector<const AllocaInst*> pointers);
            std::set<const Value *> trackPointsToSet(const Instruction *cur, const Instruction *ptr, std::map<const Instruction *, std::map<const Instruction *, std::pair<std::set<const Value *>, bool>>> pts);

    };
}







#endif
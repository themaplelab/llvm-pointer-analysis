#ifndef LLVM_TRANSFORM_UTILS_PRINT_ALIAS_TO_PAIRS_H
#define LLVM_TRANSFORM_UTILS_PRINT_ALIAS_TO_PAIRS_H

#include "llvm/IR/PassManager.h"
#include "llvm/Transforms/Utils/FlowSensitivePointerAnalysis.h"


namespace llvm{
    class PrintAliasToPairs : public PassInfoMixin<PrintAliasToPairs>{
        public:
            PreservedAnalyses run(Module &m, ModuleAnalysisManager &mam);
            void getAliasResult(const Instruction*, std::map<const Instruction *, std::map<const Instruction *, std::pair<DenseSet<const Value *>, bool>>> pts, std::vector<const AllocaInst*> pointers);
            DenseSet<const Value *> trackPointsToSet(const Instruction *cur, const Instruction *ptr, std::map<const Instruction *, std::map<const Instruction *, std::pair<DenseSet<const Value *>, bool>>> pts);
            void processAliasPairsForFunc(const Function *func, llvm::DenseMap<size_t, llvm::DenseSet<const llvm::Instruction *>> worklist, std::map<const llvm::Instruction *, std::map<const llvm::Instruction *, std::pair<DenseSet<const llvm::Value *>, bool>>> pts);
    };
}







#endif
#ifndef LLVM_TRANSFORM_PRINT_POINTS_TO_SET_H
#define LLVM_TRANSFORM_PRINT_POINTS_TO_SET_H

#include "llvm/IR/PassManager.h"
#include "llvm/Transforms/Utils/FlowSensitivePointerAnalysis.h"

namespace llvm{
    class PrintPointsToSet : public PassInfoMixin<PrintPointsToSet>{
        public:
            PreservedAnalyses run(Module &m, ModuleAnalysisManager &mam);
            void printPointsToSet(const Instruction*, std::map<const Instruction *, std::map<const Instruction *, std::pair<DenseSet<const Value *>, bool>>> pts, std::vector<const AllocaInst*> pointers);
            DenseSet<const Value *> trackPointsToSet(const Instruction *cur, const Instruction *ptr, std::map<const Instruction *, std::map<const Instruction *, std::pair<DenseSet<const Value *>, bool>>> pts);
            void processPointsToSetForFunc(const Function *func, llvm::DenseMap<size_t, llvm::DenseSet<const llvm::Instruction *>> worklist, std::map<const llvm::Instruction *, std::map<const llvm::Instruction *, std::pair<DenseSet<const llvm::Value *>, bool>>> pts);
    
    
    };
}




#endif
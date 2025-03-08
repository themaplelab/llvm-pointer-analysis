#include "llvm/Transforms/Utils/LfsPaPrint.h"
#include "llvm/Transforms/Utils/LevPA.h"


using namespace llvm;

PreservedAnalyses LfspaPrint::run(Module &m, ModuleAnalysisManager &mam){
    auto result = mam.getResult<FlowSensitivePointerAnalysis>(m);

    return PreservedAnalyses::all();
}
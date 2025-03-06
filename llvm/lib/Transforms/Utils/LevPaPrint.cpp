#include "llvm/Transforms/Utils/LevPaPrint.h"

using namespace llvm;


PreservedAnalyses LevPaPrint::run(Module &m, ModuleAnalysisManager &mam){

    auto LevPaResult = mam.getResult<LevPA>(m);

    return PreservedAnalyses::all();

}

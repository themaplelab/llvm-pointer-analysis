#include "llvm/Transforms/Utils/PointerAnalysis.h"
#include "llvm/Transforms/Utils/SteengaardAnalysis.h"

using namespace llvm;


PreservedAnalyses PointerAnalysis::run(Module &m, ModuleAnalysisManager &mam){
    // auto SteengaardAnalysisResult = mam.getResult<SteengaardAnalysis>(m);

    // auto PointerLevels = SteengaardAnalysisResult.getPointerLevels();

    // for(auto p : PointerLevels){
    //     outs() << p.first << " => " << p.second << "\n";
    // }

    return PreservedAnalyses::all();
}
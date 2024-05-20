#include "llvm/Transforms/Utils/FunctionCounter.h"

using namespace llvm;


PreservedAnalyses FunctionCounter::run(Function &f, FunctionAnalysisManager &fam){
    ++counter;
    outs() << "At function " << f.getName() << ", counter is " << counter << "\n";
    return PreservedAnalyses::all();
}




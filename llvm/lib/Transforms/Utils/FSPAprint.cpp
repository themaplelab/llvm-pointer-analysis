#include "llvm/Transforms/Utils/FSPAprint.h"

using namespace llvm;

PreservedAnalyses PrintPL::run(Module &m, ModuleAnalysisManager &mam){
    auto result = mam.getResult<FlowSensitivePointerAnalysis>(m);
    auto worklist = result.getWorkList();

    DenseMap<const Instruction*, size_t> pointerLevel;
    for(auto pair : worklist){
        for(auto pointer : pair.second){
            pointerLevel[pointer] = pair.first;
            if(dyn_cast<AllocaInst>(pointer)){
                outs() << *pointer << " => " << pair.first << "\n";
            }
        }
    }


    return PreservedAnalyses::all();
}

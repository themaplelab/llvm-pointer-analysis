#include "llvm/Transforms/Utils/FSPAprint.h"

using namespace llvm;

PreservedAnalyses PrintPL::run(Module &m, ModuleAnalysisManager &mam){
    auto result = mam.getResult<FlowSensitivePointerAnalysis>(m);
    auto worklist = result.getWorkList();

    for(auto &func : m.functions()){
        outs() << "Function " << func.getName() << "\n";
        processWorkListForFunction(worklist[&func]);
    }

    return PreservedAnalyses::all();
}


void PrintPL::processWorkListForFunction(DenseMap<size_t, llvm::DenseSet<const Value*>> worklist){
    
    DenseMap<const Value*, size_t> pointerLevel;
    for(auto pair : worklist){
        for(auto pointer : pair.second){
            pointerLevel[pointer] = pair.first;
            if(dyn_cast<AllocaInst>(pointer)){
                outs() << *pointer << " => " << pair.first << "\n";
            }
        }
    }
}
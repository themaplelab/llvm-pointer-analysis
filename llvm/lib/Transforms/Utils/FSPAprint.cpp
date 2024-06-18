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


void PrintPL::processWorkListForFunction(llvm::DenseMap<size_t, llvm::DenseSet<const llvm::Instruction *>> worklist){
    
    DenseMap<const Instruction*, size_t> pointerLevel;
    for(auto pair : worklist){
        for(auto pointer : pair.second){
            pointerLevel[pointer] = pair.first;
            if(dyn_cast<AllocaInst>(pointer)){
                outs() << *pointer << " => " << pair.first << "\n";
            }
        }
    }
}
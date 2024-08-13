#include "llvm/Transforms/Utils/FSPAprint.h"
#include "llvm/Transforms/Utils/AndersenPointerAnalysis.h"
#include "llvm/Transforms/Utils/StagedFlowSensitivePointerAnalysis.h"


using namespace llvm;

PreservedAnalyses PrintPL::run(Module &m, ModuleAnalysisManager &mam){
    auto result = mam.getResult<FlowSensitivePointerAnalysis>(m);
    // auto AndersenResult = mam.getResult<AndersenPointerAnalysis>(m);
    // auto SFSResult = mam.getResult<StagedFlowSensitivePointerAnalysis>(m);
    // auto worklist = result.getWorkList();

    return PreservedAnalyses::all();

    // for(auto &func : m.functions()){
    //     outs() << "Function " << func.getName() << "\n";
    //     processWorkListForFunction(worklist[&func]);
    // }

    // return PreservedAnalyses::all();
}


void PrintPL::processWorkListForFunction(std::map<size_t, std::set<const Value*>> worklist){
    
    std::map<const Value*, size_t> pointerLevel;
    for(auto pair : worklist){
        for(auto pointer : pair.second){
            pointerLevel[pointer] = pair.first;
            if(dyn_cast<AllocaInst>(pointer)){
                outs() << *pointer << " => " << pair.first << "\n";
            }
        }
    }
}
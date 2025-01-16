#include "llvm/Transforms/Utils/GenerateDataLogFacts.h"

using namespace llvm;

int MyMemoryObject::cnt = 0;

PreservedAnalyses GenerateDataLogFacts::run(Module &M, ModuleAnalysisManager &MAM){

    int instcnt = 0;
    int funccnt = 0;
    for(const Function &F: M){
        func2id[&F] = funccnt++;
        for(const BasicBlock &BB : F){
            size_t paraIdx = 0;
            while(paraIdx < F.arg_size()){
                auto para = getMemoryObjectFromValue(F.getArg(paraIdx));
                FormalArgs.push_back({&F, paraIdx, para});
                paraIdx++;
            }

            for(const Instruction &I : BB){
                inst2id[&I] = instcnt++;
            }
        }
    }

    for(const Function &F: M){
        for(const BasicBlock &BB : F){
            for(const Instruction &I : BB){

                if(auto Alloca = dyn_cast<AllocaInst>(&I)){
                    // %a = alloca i8*
                    // create memoryobjects for a and stack variable
                    // add them to map
                    // record Alloc relation "Alloc(instID(I), moid(a), moid(stackMemObj))"

                    auto lhs = getMemoryObjectFromValue(&I);
                    auto rhs = MyMemoryObject(&I);

                    Allocs.push_back({&I,lhs,rhs,I.getFunction()});
                }
                if(auto Load = dyn_cast<LoadInst>(&I)){
                    // %0 = load %a
                    // create memoryobject for %0, find mo for %a
                    // record Load relation "Load(instId(I), moid(%0), moid(%a))"

                    auto lhs = getMemoryObjectFromValue(&I);
                    auto rhs = getMemoryObjectFromValue(Load->getPointerOperand());
                    Loads.push_back({&I,lhs,rhs});
                }
                if(auto Store = dyn_cast<StoreInst>(&I)){
                    // store %a %b
                    auto to = getMemoryObjectFromValue(Store->getPointerOperand());
                    
                    // todo: if valiue operand is not pointer, create a new mo.
                    auto from = getMemoryObjectFromValue(Store->getValueOperand());
                    Stores.push_back({&I,to,from});
                }

                if(auto BC = dyn_cast<BitCastInst>(&I)){
                    auto lhs = getMemoryObjectFromValue(&I);
                    auto rhs = getMemoryObjectFromValue(BC->getOperand(0));
                    Moves.push_back({&I, lhs, rhs});
                }
                if(auto GEP = dyn_cast<GetElementPtrInst>(&I)){
                    auto lhs = getMemoryObjectFromValue(&I);
                    auto rhs = getMemoryObjectFromValue(GEP->getPointerOperand());
                    Moves.push_back({&I, lhs, rhs});
                }

                if(auto Call = dyn_cast<CallInst>(&I)){
                    auto calledFunc = Call->getCalledFunction();
                    if(calledFunc){
                        // todo: need to make sure calledFunc is in dict.
                        CallGraphs.push_back({&I, calledFunc});

                        size_t argIdx = 0;
                        while(argIdx < Call->arg_size()){
                            auto arg = getMemoryObjectFromValue(Call->getArgOperand(argIdx));
                            ActualArgs.push_back({calledFunc, argIdx, arg});
                            argIdx++;
                        }

                        if(!calledFunc->getReturnType()->isVoidTy()){
                            auto lhs = getMemoryObjectFromValue(&I);
                            ActualReturns.push_back({calledFunc, lhs});
                        }
                    }
                }
                if(auto Invoke = dyn_cast<InvokeInst>(&I)){
                    auto calledFunc = Invoke->getCalledFunction();
                    if(calledFunc){
                        // todo: need to make sure calledFunc is in dict.
                        CallGraphs.push_back({&I, calledFunc});

                        size_t argIdx = 0;
                        while(argIdx < Invoke->arg_size()){
                            auto arg = getMemoryObjectFromValue(Invoke->getArgOperand(argIdx));
                            ActualArgs.push_back({calledFunc, argIdx, arg});
                            argIdx++;
                        }

                       if(!calledFunc->getReturnType()->isVoidTy()){
                            auto lhs = getMemoryObjectFromValue(&I);
                            ActualReturns.push_back({calledFunc, lhs});
                        }
                    }
                }

                if(auto PhiNode = dyn_cast<PHINode>(&I)){
                    auto from1 = getMemoryObjectFromValue(PhiNode->getOperand(0));
                    auto from2 = getMemoryObjectFromValue(PhiNode->getOperand(1));
                    auto to = getMemoryObjectFromValue(&I);
                    Phis.push_back({&I, to, from1, from2});
                }

                if(auto Return = dyn_cast<ReturnInst>(&I)){
                    if(Return->getReturnValue()){
                        auto ret = getMemoryObjectFromValue(Return->getReturnValue());
                        FormalReturns.push_back({Return->getFunction(), ret});
                    }
                    
                }
            }
        }
    }

    // outs() << "ALLOC\n";
    // for(auto t : Allocs){
    //     outs() << "Alloc(" << inst2id[std::get<0>(t)] << ", " << std::get<1>(t).getId() << ", " << std::get<2>(t).getId() << ", " << func2id[std::get<3>(t)] << ")\n";
    // }
    dumpAllocs();

    // outs() << "LOAD" << std::string(50,'-') << "\n";
    // for(auto t : Loads){
    //     outs() << "Load(" << inst2id[std::get<0>(t)] << ", " << std::get<1>(t).getId() << ", " << std::get<2>(t).getId() << ")\n";
    // }
    dumpLoads();

    // outs() << "STORE" << std::string(50,'-') << "\n";
    // for(auto t : Stores){
    //     outs() << "Store(" << inst2id[std::get<0>(t)] << ", " << std::get<1>(t).getId() << ", " << std::get<2>(t).getId() << ")\n";
    // }
    dumpStores();

    // outs() << "MOVE" << std::string(50,'-') << "\n";
    // for(auto t : Moves){
    //     outs() << "Move(" << inst2id[std::get<0>(t)] << ", " << std::get<1>(t).getId() << ", " << std::get<2>(t).getId() << ")\n";
    // }
    dumpMoves();

    // outs() << "CALLGRAPH" << std::string(50,'-') << "\n";
    // for(auto t : CallGraphs){
    //     outs() << "CALLGRAPH(" << inst2id[std::get<0>(t)] << ", " << func2id[std::get<1>(t)] << ")\n";
    // }
    dumpCallGraphs();

    // outs() << "PHI NODES" << std::string(50,'-') << "\n";
    // for(auto t : Phis){
    //     outs() << "PHI(" << inst2id[std::get<0>(t)] << ", " << std::get<1>(t).getId() << ", " << std::get<2>(t).getId() << ", " << std::get<3>(t).getId() << ")\n";
    // }
    dumpPhis();

    // outs() << "FORMAL ARGS" << std::string(50,'-') << "\n";
    // for(auto t : FormalArgs){
    //     outs() << "FORMALARG(" << func2id[std::get<0>(t)] << ", " << std::get<1>(t) << ", " << std::get<2>(t).getId() << ")\n";
    // }
    dumpFormalArgs();

    // outs() << "ACTUAL ARGS" << std::string(50,'-') << "\n";
    // for(auto t : ActualArgs){
    //     outs() << "ACTUALARG(" << func2id[std::get<0>(t)] << ", " << std::get<1>(t) << ", " << std::get<2>(t).getId() << ")\n";
    // }
    dumpActualArgs();

    outs() << "FORMAL RETURNS" << std::string(50,'-') << "\n";

    for(auto t : FormalReturns){
        outs() << "FORMALRETURN(" << func2id[std::get<0>(t)] << ", " << std::get<1>(t).getId() << ")\n";
    }

    outs() << "ACTUAL RETURNS" << std::string(50,'-') << "\n";

    for(auto t : ActualReturns){
        outs() << "ACTUALRETURN(" << func2id[std::get<0>(t)] << ", " << std::get<1>(t).getId() << ")\n";
    }

    outs() << "MEMORYOBJECT" << std::string(50,'-') << "\n";

    for(auto p : var2mo){
        outs() << p.second.getId() << " " << *(p.first) << "\n";
    }

    outs() << "INSTRUCTION" << std::string(50,'-') << "\n";

    for(auto p : inst2id){
        outs() << p.second << " " << *(p.first) << "\n";
    }

    outs() << "FUNCTION" << std::string(50,'-') << "\n";

    for(auto p : func2id){
        outs() << p.second << " " << p.first->getName().str() << "\n";
    }



    return PreservedAnalyses::all();

}


MyMemoryObject GenerateDataLogFacts::getMemoryObjectFromValue(const Value *value){
    if(var2mo.count(value)){
        return var2mo[value];
    }

    auto mo = MyMemoryObject(value);
    var2mo[value] = mo;
    return mo;

}

std::vector<const Instruction*> GenerateDataLogFacts::getNextInstructions(const Instruction *inst){

    auto res = std::vector<const Instruction*>();

    auto next = inst->getNextNonDebugInstruction();
    if(!next){
        auto bb = inst->getParent();
        for(auto nextBB : successors(bb)){
            const Instruction *nextInst = nullptr;
            for(const Instruction &inst : *nextBB){
                if(!isa<DbgInfoIntrinsic>(inst)){
                    nextInst = &inst;
                    break;
                }
            }
            if(nextInst){
                res.push_back(nextInst);
            }
        }
    }

    return res;
}

void GenerateDataLogFacts::dumpAllocs(){

    std::error_code EC;

    raw_fd_ostream outFile("/Users/jiaqi/Documents/Maple/Research/GPU-FSPA/Souffle/facts/Alloc.facts", EC, sys::fs::OF_Text);
    if(EC){
        llvm::errs() << "Error opening file " << "Alloc.facts" << ": " << EC.message() << "\n";
        return;
    }

    for(auto t : Allocs){
        outFile << std::get<1>(t).getId() << ", " << std::get<2>(t).getId() << ", " << func2id[std::get<3>(t)] << "\n";
    }
}

void GenerateDataLogFacts::dumpLoads(){

    std::error_code EC;

    raw_fd_ostream outFile("/Users/jiaqi/Documents/Maple/Research/GPU-FSPA/Souffle/facts/Load.facts", EC, sys::fs::OF_Text);
    if(EC){
        llvm::errs() << "Error opening file " << "Load.facts" << ": " << EC.message() << "\n";
        return;
    }

    for(auto t : Loads){
        outFile << std::get<1>(t).getId() << ", " << std::get<2>(t).getId() << "\n";
    }
}

void GenerateDataLogFacts::dumpStores(){

    std::error_code EC;

    raw_fd_ostream outFile("/Users/jiaqi/Documents/Maple/Research/GPU-FSPA/Souffle/facts/Store.facts", EC, sys::fs::OF_Text);
    if(EC){
        llvm::errs() << "Error opening file " << "Store.facts" << ": " << EC.message() << "\n";
        return;
    }

    for(auto t : Stores){
        outFile << std::get<1>(t).getId() << ", " << std::get<2>(t).getId() << "\n";
    }
}

void GenerateDataLogFacts::dumpMoves(){

    std::error_code EC;

    raw_fd_ostream outFile("/Users/jiaqi/Documents/Maple/Research/GPU-FSPA/Souffle/facts/Move.facts", EC, sys::fs::OF_Text);
    if(EC){
        llvm::errs() << "Error opening file " << "Move.facts" << ": " << EC.message() << "\n";
        return;
    }

    for(auto t : Moves){
        outFile << std::get<1>(t).getId() << ", " << std::get<2>(t).getId() << "\n";
    }
}

void GenerateDataLogFacts::dumpCallGraphs(){

    std::error_code EC;

    raw_fd_ostream outFile("/Users/jiaqi/Documents/Maple/Research/GPU-FSPA/Souffle/facts/CallGraph.facts", EC, sys::fs::OF_Text);
    if(EC){
        llvm::errs() << "Error opening file " << "CallGraph.facts" << ": " << EC.message() << "\n";
        return;
    }

    for(auto t : CallGraphs){
        outFile << inst2id[std::get<0>(t)] << ", " << func2id[std::get<1>(t)] << "\n";
    }
}

void GenerateDataLogFacts::dumpPhis(){

    std::error_code EC;

    raw_fd_ostream outFile("/Users/jiaqi/Documents/Maple/Research/GPU-FSPA/Souffle/facts/Phi.facts", EC, sys::fs::OF_Text);
    if(EC){
        llvm::errs() << "Error opening file " << "Phi.facts" << ": " << EC.message() << "\n";
        return;
    }

    for(auto t : Phis){
        outFile << std::get<1>(t).getId() << ", " << std::get<2>(t).getId() << ", " << std::get<3>(t).getId() << "\n";
    }
}

void GenerateDataLogFacts::dumpFormalArgs(){

    std::error_code EC;

    raw_fd_ostream outFile("/Users/jiaqi/Documents/Maple/Research/GPU-FSPA/Souffle/facts/FormalArg.facts", EC, sys::fs::OF_Text);
    if(EC){
        llvm::errs() << "Error opening file " << "FormalArg.facts" << ": " << EC.message() << "\n";
        return;
    }

    for(auto t : FormalArgs){
        outFile << func2id[std::get<0>(t)] << ", " << std::get<1>(t) << ", " << std::get<2>(t).getId() << "\n";
    }
}

void GenerateDataLogFacts::dumpActualArgs(){

    std::error_code EC;

    raw_fd_ostream outFile("/Users/jiaqi/Documents/Maple/Research/GPU-FSPA/Souffle/facts/ActualArg.facts", EC, sys::fs::OF_Text);
    if(EC){
        llvm::errs() << "Error opening file " << "ActualArg.facts" << ": " << EC.message() << "\n";
        return;
    }

    for(auto t : ActualArgs){
        outFile << func2id[std::get<0>(t)] << ", " << std::get<1>(t) << ", " << std::get<2>(t).getId() << "\n";
    }
}
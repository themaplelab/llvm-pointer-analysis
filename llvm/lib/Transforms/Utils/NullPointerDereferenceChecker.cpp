#include "llvm/Transforms/Utils/NullPointerDereferenceChecker.h"


using namespace llvm;


PreservedAnalyses NullPointerChecker::run(Module &M, ModuleAnalysisManager &MAM){
    auto Result = MAM.getResult<FlowSensitivePointerAnalysis>(M);
    auto PTS = Result.getPointsToSet();
    auto Worklist = Result.getWorkList();
    auto Func2AllocatedPointersAndParameterAliases = Result.getFunc2Pointers();


    for(auto &Func : M.functions()){
        auto Pointers = SetVector<const Value*>();
        if(Func2AllocatedPointersAndParameterAliases.count(&Func)){
            Pointers = Func2AllocatedPointersAndParameterAliases[&Func];
        }

        CheckNullPtrAtFunc(&Func, Pointers, PTS);
    }

    return PreservedAnalyses::all();

}



void NullPointerChecker::CheckNullPtrAtFunc(const Function *Func, const SetVector<const Value*> &Pointers, 
    std::map<const Instruction *, std::map<const Value*, std::set<const Value*>>> &PTS){

    for(auto &Inst : instructions(*Func)){
        if(auto Store = dyn_cast<StoreInst>(&Inst)){
            auto Ptr = Store->getPointerOperand();
            if(PTS.count(Store) && PTS.at(Store).count(Ptr)){
                if(PTS.at(Store).at(Ptr).count(nullptr)){
                    dbgs() << "ERROR: Found nullptr dereference at" << *Store << ", trying to dereferencing pointer "
                        << *Ptr << ".\n";
                }
            }
        }
        else if(auto Load = dyn_cast<LoadInst>(&Inst)){
            auto Ptr = Load->getPointerOperand();
            if(PTS.count(Load) && PTS.at(Load).count(Ptr)){
                if(PTS.at(Load).at(Ptr).count(nullptr)){
                    dbgs() << "ERROR: Found nullptr dereference at" << *Load << ", trying to dereferencing pointer "
                        << *Ptr << ".\n";
                }
            }
        }
    }




}

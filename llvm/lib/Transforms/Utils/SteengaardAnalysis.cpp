#include "llvm/Transforms/Utils/SteengaardAnalysis.h"

#include "llvm/IR/Instructions.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/Support/raw_ostream.h"


using namespace llvm;

AnalysisKey SteengaardAnalysis::Key;

size_t SteengaardAnalysis::id = 0;


SteengaardAnalysisResult SteengaardAnalysis::run(Module &M, ModuleAnalysisManager &MAM){

    // Create Id for nullptr;
    getID(nullptr, true);

    for(auto &F : M){
        for(auto &Inst : instructions(F)){
            if(auto Alloca = dyn_cast<AllocaInst>(&Inst)){
                auto topLevel = getID(Alloca, true);
                auto AddrTaken = getID(Alloca, false);
                Uf.find(topLevel);
                Uf.find(AddrTaken);
                Pts.try_emplace(topLevel, AddrTaken);
            }
            else if(auto Load = dyn_cast<LoadInst>(&Inst)){

                if(!Load->getType()->isPointerTy()){
                    continue;
                }

                auto PointerOp = getID(Load->getPointerOperand(), true);
                auto PtsKey = Uf.find(PointerOp);
                if(Pts.count(PtsKey)){
                    Uf.merge(Uf.find(getID(Load, true)), Uf.find(Pts.at(PtsKey)));
                }
            }
            else if(auto Store = dyn_cast<StoreInst>(&Inst)){
                // outs() << "Store: " << *Store << "\n";
                if(!Store->getValueOperand()->getType()->isPointerTy()){
                    // outs() << "cont\n";
                    continue;
                }

                auto PointerOp = getID(Store->getPointerOperand(), true);
                auto ValueOp = getID(Store->getValueOperand(), true);
                auto PtsKey = Uf.find(PointerOp);
                if(Pts.count(PtsKey)){
                    Uf.merge(Uf.find(Pts.at(PtsKey)), Uf.find(ValueOp));
                }
            }
            else if(auto BitCast = dyn_cast<BitCastInst>(&Inst)){
                

                if(BitCast->getType()->isPointerTy()){
                    auto Lhs = getID(BitCast, true);
                    auto Rhs = getID(BitCast->getOperand(0), true);
                    Uf.merge(Uf.find(Lhs), Uf.find(Rhs));
                }
            }
            else if(auto GEP = dyn_cast<GetElementPtrInst>(&Inst)){
                
                if(GEP->getType()->isPointerTy()){
                    auto Lhs = getID(GEP, true);
                    auto Rhs = getID(GEP->getOperand(0), true);
                    Uf.merge(Uf.find(Lhs), Uf.find(Rhs));
                }
            }
            else if(auto Call = dyn_cast<CallBase>(&Inst)){
                if(!Call->getCalledFunction() || Call->getCalledFunction()->isDeclaration()){
                    continue;
                }
                // para-arg passing
                size_t i = 0;
                while(i < Call->arg_size()){
                    auto Para = getID(Call->getCalledFunction()->getArg(i), true);
                    auto Arg = getID(Call->getOperand(i), true);
                    Uf.merge(Uf.find(Para), Uf.find(Arg));
                    ++i;
                }


                // return-call passing
                // todo: optimize. not efficient.
                for(auto &Ret : instructions(Call->getCalledFunction())){
                    if(auto Return = dyn_cast<ReturnInst>(&Ret)){
                        if(Return->getReturnValue() && Return->getReturnValue()->getType()->isPointerTy()){
                            Uf.merge(Uf.find(getID(Call, true)), Uf.find(getID(Return->getReturnValue(), true)));
                        }
                    }
                }
            }
        }
    }

    

    computePtsAndAlias();
    auto MaxPl = computeMaxPointerLevel();

    printStats();
    // outs() << "Pointer ID:\n";
    // for(auto p : pointerID){
    //     if(!p.first.first){
    //         outs() << "nullptr " << p.first.second << " => " << p.second << "\n";
    //     }
    //     else{
    //         outs() << *p.first.first << " " << p.first.second << " => " << p.second << "\n";
    //     }
    // }
    
    Result AnalysisResult(truePts, trueAlias, PointerLevel, pointerID, ID2Ptr, MaxPl);

    return AnalysisResult;

}


void SteengaardAnalysis::createID(const Value *Ptr, bool isTopLevel){
    pointerID.try_emplace({Ptr,isTopLevel}, id);
    ID2Ptr.try_emplace(id++, std::make_pair(Ptr,isTopLevel));
}


size_t SteengaardAnalysis::getID(const Value *Ptr, bool isTopLevel){
    if(pointerID.find({Ptr, isTopLevel}) == pointerID.end()){
        createID(Ptr, isTopLevel);
    }

    return pointerID.at({Ptr, isTopLevel});
}


size_t SteengaardAnalysis::getPointerLevel(size_t Pointer){

    if(!truePts.count(Uf.find(Pointer))){
        PointerLevel.try_emplace(Pointer, 0);
        return PointerLevel.at(Pointer);
    }

    if(PointerLevel.count(Uf.find(Pointer))){
        PointerLevel.try_emplace(Pointer, PointerLevel.at(Uf.find(Pointer)));
        return PointerLevel.at(Pointer);
    }

    size_t maxPl = 0;
    for(auto pointee : truePts.at(Uf.find(Pointer))){
        maxPl = std::max(maxPl, getPointerLevel(pointee));
    }

    PointerLevel.try_emplace(Pointer, maxPl+1);
    return PointerLevel.at(Pointer);

}

void SteengaardAnalysis::computePtsAndAlias(){
    for(auto p : Uf.getParent()){
        auto Key = p.first;
        auto PtsClass = Uf.find(Key);
        trueAlias[PtsClass].insert(Key);
        if(Pts.count(Key)){
            truePts[PtsClass].insert(Pts.at(Key));
        }
    }
}

size_t SteengaardAnalysis::computeMaxPointerLevel(){
    size_t maxPl = 0;
    for(auto p : Uf.getParent()){
        maxPl = std::max(maxPl, getPointerLevel(p.first));
    }
    return maxPl;
}

void SteengaardAnalysis::printStats(){

    outs() << "Pointer ID:\n";
    for(auto p : pointerID){
        if(!p.first.first){
            outs() << "nullptr " << p.first.second << " => " << p.second << "\n";
        }
        else{
            outs() << *p.first.first << " " << p.first.second << " => " << p.second << "\n";
        }
    }
     
    outs() << "Parents\n";
    for(auto p : Uf.getParent()){
        outs() << p.first << " => " << p.second << "\n";
        
    }

    outs() << "pts\n";
    for(auto p : truePts){
        outs() << "{";
        for(auto alias : trueAlias.at(p.first)){
            outs() << alias << " ";
        }
        outs() << "} => ";

        outs() << "{";
        for(auto pointee : truePts.at(p.first)){
            outs() << pointee << " ";
        }
        outs() << "}\n";
    }

    outs() << "pointer level\n";
    for(auto p : Uf.getParent()){
        outs() << p.first << " => " << getPointerLevel(p.first) << "\n";
    }

    // outs() << "id2Ptr\n";
    // for(auto p : ID2Ptr){
    //     outs() << p.first << " => " << *p.second.first << " " << p.second.second << "\n";
    // }


}
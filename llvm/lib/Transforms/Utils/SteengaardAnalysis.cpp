#include "llvm/Transforms/Utils/SteengaardAnalysis.h"

#include "llvm/IR/Instructions.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/Support/raw_ostream.h"

#include <fstream>
#include <sstream>



using namespace llvm;

AnalysisKey SteengaardAnalysis::Key;

size_t SteengaardAnalysis::id = 0;

size_t getPl(const Value *v, bool istop){
    auto Type = v->getType();
    auto res = 0;
    while(Type->isPointerTy()){
        Type = Type->getPointerElementType();
        res += 1;
    }

    return istop ? res : res-1;
}

void SteengaardAnalysis::SCCtoDAG(){
    
    for(auto p : truePts){
        if(!Visited.count(p.first)){
            findSCC(p.first);
        }
    }

    for(auto p : truePts){
        for(auto to : p.second){
            if(LowLink[p.first] != LowLink[to]){
                RealPts[LowLink[p.first]].insert(LowLink[to]);
            }
        }
    }
}

void SteengaardAnalysis::findSCC(size_t node){

    // outs() << "Find scc " << node << "\n";

    Visited.insert(node);
    IndexOf[node] = index;
    LowLink[node] = index;
    ++index;
    Stack.push(node);
    OnStack[node] = 1;

    if(truePts.count(node)){
        for(auto to : truePts.at(node)){
            if(!Visited.count(to)){
                findSCC(to);
                LowLink[node] = std::min(LowLink[node], LowLink[to]);
            }
            else if(OnStack[to]){
                LowLink[node] = std::min(LowLink[node], LowLink[to]);
            }
        }
    }

    if(LowLink[node] == IndexOf[node]){
        while(true){
            auto n = Stack.top();
            Stack.pop();
            OnStack[n] = 0;
            SCC2Node[IndexOf[node]].insert(n);
            if(n == node){
                break;
            }
        }
    }
    
}

SteengaardAnalysisResult SteengaardAnalysis::run(Module &M, ModuleAnalysisManager &MAM){


    // Create Id for nullptr;
    getID(nullptr, true);

    for(auto &F : M){
        for(auto &Inst : instructions(F)){
            // outs() << Inst << "\n";
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
                    auto Lhs = getID(GEP, true);
                    auto Rhs = getID(GEP->getOperand(0), true);
                    Uf.merge(Uf.find(Lhs), Uf.find(Rhs));
                
            }
            else if(auto Call = dyn_cast<CallBase>(&Inst)){
                if(!Call->getCalledFunction() || Call->getCalledFunction()->isDeclaration() || Call->getFunctionType()->isVarArg()){
                    // Do not process variadic arguments.
                    continue;
                }
                // para-arg passing
                size_t i = 0;
                while(i < Call->arg_size()){
                    // outs() << *Call << " " << i << "\n";
                    auto Para = getID(Call->getCalledFunction()->getArg(i), true);
                    auto Arg = getID(Call->getArgOperand(i), true);
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
            // outs() << "end\n";
        }
    }

    computePtsAndAlias();
    SCCtoDAG();
    auto MaxPl = computeMaxPointerLevel();


    DEBUG_WITH_TYPE("steengaard", verifyResult(M));
    
    Result AnalysisResult(truePts, trueAlias, PointerLevel, pointerID, ID2Ptr, MaxPl);

    llvm_unreachable("test");

    return AnalysisResult;

}

void SteengaardAnalysis::verifyResult(Module &M){
    auto SourceFileName = M.getSourceFileName();
    auto DotPosition = SourceFileName.rfind('.');
    auto ExpectedOutPutFileName = SourceFileName.substr(0, DotPosition) + ".out";

    std::ifstream ifs(ExpectedOutPutFileName);
    std::string Line;
    std::map<size_t, size_t> ExpectedPointerLevel2Count;
    while(std::getline(ifs, Line)){

        std::istringstream iss(Line);
        std::vector<size_t> Nums;
        std::string Num;

        while(std::getline(iss, Num, ',')){
            Nums.push_back(std::stoul(Num));
        }
        ExpectedPointerLevel2Count[Nums[0]] = Nums[1];
    }


    // analysis result
    std::map<size_t, size_t> PointerLevel2Count;
    for(auto p : Uf.getParent()){
        PointerLevel2Count[getPointerLevel(p.first)] += 1;
    }

    auto CorrectAnswer = (ExpectedPointerLevel2Count.size() == PointerLevel2Count.size() && std::equal(ExpectedPointerLevel2Count.begin(), ExpectedPointerLevel2Count.end(), PointerLevel2Count.begin()));
    if(!CorrectAnswer){
        // DEBUG_WITH_TYPE("steengaard", printStats());
        outs() << "Expected\n";
        for(auto p : ExpectedPointerLevel2Count){
            outs() << "Pointer level " << p.first << " contains " << p.second << "pointers\n";
        }
        outs() << "Actual\n";
        for(auto p : PointerLevel2Count){
            outs() << "Pointer level " << p.first << " contains " << p.second << "pointers\n";
        }
        llvm_unreachable("Incorrect pointer level.");
    }
    else{
        DEBUG_WITH_TYPE("steengaard", outs() << "Steengaard test passed.\n");
    }

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
    return getPointerLevelForSCCGraph(LowLink[Uf.find(Pointer)]);
}

size_t SteengaardAnalysis::getPointerLevelForSCCGraph(size_t SCCNode){
    // get pointer level for scc group

    if(PointerLevel.count(SCCNode)){
        return PointerLevel.at(SCCNode);
    }

    if(RealPts[SCCNode].empty()){
        PointerLevel.try_emplace(SCCNode, 0);
        return PointerLevel.at(SCCNode);
    }

    size_t maxPl = 0;
    for(auto Pointee : RealPts[SCCNode]){
        maxPl = std::max(maxPl, getPointerLevelForSCCGraph(Pointee));
    }
    PointerLevel.try_emplace(SCCNode, maxPl+1);
    return PointerLevel.at(SCCNode);

}

void SteengaardAnalysis::computePtsAndAlias(){
    for(auto p : Uf.getParent()){
        auto Key = p.first;
        auto PtsClass = Uf.find(Key);
        trueAlias[PtsClass].insert(Key);
        if(Pts.count(Key)){
            truePts[PtsClass].insert(Uf.find(Pts.at(Key)));
        }
    }
}

size_t SteengaardAnalysis::computeMaxPointerLevel(){
    size_t maxPl = 0;
    for(auto p : Uf.getParent()){
        maxPl = std::max(maxPl, getPointerLevelForSCCGraph(LowLink[Uf.find(p.first)]));
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

    outs() << "Pts\n";
    for(auto p : Pts){
        outs() << p.first << " " << p.second << "\n";
    }
     
    outs() << "Parents\n";
    for(auto p : Uf.getParent()){
        outs() << p.first << " => " << p.second << "\n";
        
    }

    outs() << "truepts\n";
    for(auto p : truePts){
        outs() << "{";
        for(auto alias : trueAlias.at(Uf.find(p.first))){
            outs() << alias << " ";
        }
        outs() << "} => ";

        outs() << "{";
        for(auto pointee : truePts.at(Uf.find(p.first))){
            outs() << pointee << " ";
        }
        outs() << "}\n";
    }

    outs() << "sccNode pointer level\n";
    for(auto p : PointerLevel){
        outs() << p.first << " => " << p.second << "\n";
    }

    for(auto p : Uf.getParent()){
        outs() << p.first  << " has pointer level " << getPointerLevel(p.first) << "\n";
    }

    // outs() << "id2Ptr\n";
    // for(auto p : ID2Ptr){
    //     outs() << p.first << " => " << *p.second.first << " " << p.second.second << "\n";
    // }


}
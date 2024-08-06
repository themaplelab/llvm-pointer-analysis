#include "llvm/Transforms/Utils/AndersenPointerAnalysis.h"

using namespace llvm;

AndersenPointerAnalysisResult AndersenPointerAnalysis::run(Module &M, ModuleAnalysisManager &MAM){

    for(auto &Func : M.functions()){
        for(auto &Inst : instructions(Func)){
            if(auto Store = dyn_cast<StoreInst>(&Inst)){
                if(isa<AllocaInst>(Store->getPointerOperand())){
                    if(isa<AllocaInst>(Store->getValueOperand()) || isa<Argument>(Store->getValueOperand())
                        || !Store->getValueOperand()->getType()->isPointerTy()){
                        PointsToSet[Store->getPointerOperand()].insert(Store->getValueOperand());
                        WorkList[&Func].insert(Store->getPointerOperand());
                    }
                    else if(auto BitCast = dyn_cast<BitCastInst>(Store->getValueOperand())){
                        PointsToSet[Store->getPointerOperand()].insert(BitCast->stripPointerCasts());
                        WorkList[&Func].insert(Store->getPointerOperand());
                    }
                    
                }
            }
            if(auto Call = dyn_cast<CallInst>(&Inst)){
                if(!Call->getCalledFunction() || Call->getCalledFunction()->isDeclaration()){
                    continue;
                }
                Func2CallSites[Call->getCalledFunction()].insert(Call);
            }
        }
    }

    // for(auto Pair : WorkList){
    //     dbgs() << Pair.first->getName() << "\n";
    //     for(auto E : Pair.second){
    //         dbgs() << *E << "\n";
    //     }
    // }

    std::set<const Function*> FunctionWorkList{};

    for(auto Pair : WorkList){
        if(!Pair.second.empty()){
            FunctionWorkList.insert(Pair.first);
        }
    }

    while(!FunctionWorkList.empty()){
        auto AnalyzedFunc = *FunctionWorkList.begin();
        while(!WorkList[AnalyzedFunc].empty()){
            auto Ptr = *WorkList[AnalyzedFunc].begin();
            // dbgs() << "Processing pointer " << *Ptr << "\n";

            for(auto User : Ptr->users()){
                // dbgs() << "User " << *User << "\n";
                if(auto Load = dyn_cast<LoadInst>(User)){
                    for(auto FromPtr : PointsToSet[Load->getPointerOperand()]){
                        if(CG.addEdge(FromPtr, Load)){
                            // dbgs() << "Add edge " << *FromPtr << " => " << *Load << "\n";
                            WorkList[AnalyzedFunc].insert(FromPtr);
                            WorkList[AnalyzedFunc].insert(Load);

                        }
                    }
                }
                else if(auto Store = dyn_cast<StoreInst>(User)){
                    auto ValOperand = Store->getValueOperand();
                    auto PtrOperand = Store->getPointerOperand();

                    if(auto ValLoad = dyn_cast<LoadInst>(ValOperand)){
                        if(auto ValAlloca = dyn_cast<AllocaInst>(PtrOperand)){
                            auto FromPtr = ValLoad->getPointerOperand();
                            if(CG.addEdge(FromPtr, ValAlloca)){
                                // dbgs() << "Add edge " << *FromPtr << " => " << *ValAlloca << "\n";
                                WorkList[AnalyzedFunc].insert(FromPtr);
                            }
                        }
                        else if(auto PtrLoad = dyn_cast<LoadInst>(PtrOperand)){
                            auto FromPtr = ValLoad->getPointerOperand();
                            for(auto ToPtr : PointsToSet[PtrLoad->getPointerOperand()]){
                                if(CG.addEdge(FromPtr, ToPtr)){
                                    // dbgs() << "Add edge " << *FromPtr << " => " << *ToPtr << "\n";
                                    WorkList[AnalyzedFunc].insert(FromPtr);
                                }
                            }
                        }
                    }
                    else if(auto ValAlloca = dyn_cast<AllocaInst>(ValOperand)){
                        if(auto PtrLoad = dyn_cast<LoadInst>(PtrOperand)){
                            for(auto Pointer : PointsToSet[PtrLoad->getPointerOperand()]){
                                PointsToSet[Pointer].insert(ValAlloca);
                            }
                        }
                    }
                    else if(!ValOperand->getType()->isPointerTy()){
                        if(auto PtrLoad = dyn_cast<LoadInst>(PtrOperand)){
                            for(auto Pointer : PointsToSet[PtrLoad->getPointerOperand()]){
                                PointsToSet[Pointer].insert(ValOperand);
                            }
                        }
                    }
                }
                else if(auto Call = dyn_cast<CallInst>(User)){

                    if(!Call->getCalledFunction() || Call->getCalledFunction()->isDeclaration()){
                        continue;
                    }

                    auto ArgIdx = 0;
                    for(auto Arg : Call->operand_values()){
                        if(Arg == Ptr){
                            break;
                        }
                        ArgIdx++;
                    }

                    const Argument *Para;
                    for(auto &P : Call->getCalledFunction()->args()){
                        if(!ArgIdx){
                            Para = &P;
                            break;
                        }
                        ArgIdx--;
                    }
                    if(!Para->getType()->isPointerTy()){
                        continue;
                    }
                    PointsToSet[Para].insert(PointsToSet[Ptr].begin(), PointsToSet[Ptr].end());
                    WorkList[Call->getCalledFunction()].insert(Para);
                    FunctionWorkList.insert(Call->getCalledFunction());
                }

            }

            for(auto ToNode : CG.getEdges(Ptr)){
                auto OldSize = PointsToSet[ToNode].size();
                PointsToSet[ToNode].insert(PointsToSet[Ptr].begin(), PointsToSet[Ptr].end());

                // dbgs() << "PTS(" << *ToNode << ")\n";
                // for(auto P : PointsToSet[ToNode]){
                //     dbgs() << *P << "\n";
                // }

                if(OldSize != PointsToSet[ToNode].size()){
                    WorkList[AnalyzedFunc].insert(ToNode);
                }

                if(auto Para = dyn_cast<Argument>(ToNode)){
                    if(!Para->getType()->isPointerTy()){
                        continue;
                    }
                    auto ArgIdx = 0;
                    for(auto &P : AnalyzedFunc->args()){
                        if(&P == Para){
                            break;
                        }
                        ++ArgIdx;
                    }

                    for(auto CallSite : Func2CallSites[AnalyzedFunc]){
                        auto Arg = CallSite->getOperand(ArgIdx);

                        if(auto Load = dyn_cast<LoadInst>(Arg)){
                            for(auto P : PointsToSet[Load->getPointerOperand()]){
                                auto OldSize = PointsToSet[P].size();
                                PointsToSet[P].insert(PointsToSet[Para].begin(), PointsToSet[Para].end());
                                if(OldSize != PointsToSet[P].size()){
                                    WorkList[CallSite->getFunction()].insert(P);
                                    FunctionWorkList.insert(CallSite->getFunction());
                                }
                            }
                        
                        }
                        else{
                            auto OldSize = PointsToSet[Arg].size();

                            // dbgs() << "PTS(" << *Arg << ")\n";
                            // for(auto P : PointsToSet[Arg]){
                            //     dbgs() << *P << "\n";
                            // }
                            PointsToSet[Arg].insert(PointsToSet[Para].begin(), PointsToSet[Para].end());

                            if(OldSize != PointsToSet[Arg].size()){
                                WorkList[CallSite->getFunction()].insert(Arg);
                                FunctionWorkList.insert(CallSite->getFunction());
                            }

                        }
                    }
                }
            }
        
            WorkList[AnalyzedFunc].erase(Ptr);
        }
        FunctionWorkList.erase(AnalyzedFunc);
    }


    AnalysisResult.setPointsToSet(PointsToSet);

    dbgs() << "Points-to set for andersen analysis.\n";
    for(auto Pair : PointsToSet){
        dbgs() << *Pair.first << " =>\n";
        for(auto Ptr : Pair.second){
            dbgs() << "\t" << *Ptr << "\n";
        }
    }

    return AnalysisResult;

}


AnalysisKey AndersenPointerAnalysis::Key;
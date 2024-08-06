#include "llvm/Transforms/Utils/StagedFlowSensitivePointerAnalysis.h"

#include "llvm/Transforms/Utils/AndersenPointerAnalysis.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"



using namespace llvm;


AnalysisKey StagedFlowSensitivePointerAnalysis::Key;

void StagedFlowSensitivePointerAnalysis::printPointsToSetAtProgramLocation(const ProgramLocationTy *Loc){

    if(PointsToSetOut.count(Loc)){
        dbgs() << "At program location" << *Loc << ":\n";
        for(auto PtsForPtr : PointsToSetOut.at(Loc)){
            if(dyn_cast<Argument>(PtsForPtr.first)){
                dbgs() << "\t" << *(PtsForPtr.first) << " ==>\n";
            }
            else{
                dbgs() << *(PtsForPtr.first) << " ==>\n";
            }
            
            for(auto Pointee : PtsForPtr.second){
                if(!Pointee){
                    dbgs() << "\t " << "nullptr" << "\n";
                }
                else if(dyn_cast<Instruction>(Pointee)){
                    dbgs() << "\t" << *Pointee << "\n";
                }
                else{
                    dbgs() << "\t " << *Pointee << "\n";
                }
            }
        }
    }

}

void StagedFlowSensitivePointerAnalysis::dumpPointsToSet(){
    dbgs() << "Print sfs points-to set stats\n";
    // C++26 will treat _ as a special value that does not cause unused warning.
    for(auto PtsForPtr : PointsToSetOut){
        printPointsToSetAtProgramLocation(PtsForPtr.first);
    }
}

void StagedFlowSensitivePointerAnalysis::initialize(const Function *Func){

    WorkListTy WorkList;

    if(!Func->isDeclaration()){
        auto FirstInst = Func->getEntryBlock().getFirstNonPHIOrDbg();
        for(auto &Arg : Func->args()){
            if(!Arg.getType()->isPointerTy()){
                continue;
            }
            LabelMap[FirstInst].insert(Label(&Arg, Label::LabelType::Def));
            DefLocations[&Arg][Func].insert(FirstInst);
            PointsToSetOut[FirstInst][&Arg] = std::set<const Value*>{};
            WorkList.insert(&Arg);
        }
    }

    for(auto &Inst : instructions(*Func)){
        if(const AllocaInst *Alloca = dyn_cast<AllocaInst>(&Inst)){
            WorkList.insert(Alloca);
            LabelMap[Alloca].insert(Label(Alloca, Label::LabelType::Def));
            DefLocations[Alloca][Func].insert(Alloca);
            // A -> nullptr means A is not initialized. It helps us to find dereference of nullptr.
            PointsToSetOut[&Inst][Alloca] = std::set<const Value*>{nullptr};
        }
        else if(const CallInst *Call = dyn_cast<CallInst>(&Inst)){
            if(Call->getCalledFunction()){
                Func2CallerLocation[Call->getCalledFunction()].insert(Call);
            }
        }
        else if(const ReturnInst *Return = dyn_cast<ReturnInst>(&Inst)){
            Func2TerminateBBs[Func].insert(Return->getParent());
            for(auto &Arg : Func->args()){
                LabelMap[Return].insert(Label(&Arg, Label::LabelType::Use));
                UseList[&Arg].insert(Return);
            }
            
        }
    }

    Func2WorkList.emplace(Func, WorkList);
    return;
}

void StagedFlowSensitivePointerAnalysis::globalInitialize(Module &M){

    for(auto &Func : M.functions()){
        initialize(&Func);
    }
    return;
}

void StagedFlowSensitivePointerAnalysis::markLabelsForFunc(const Function *Func, std::map<const Value*, std::set<const Value *>> &PTS){

    for(auto &Inst : instructions(*Func)){
        if(auto Store = dyn_cast<StoreInst>(&Inst)){
            auto PtrOperand = Store->getPointerOperand();
            // store ? x
            if(isa<AllocaInst>(PtrOperand)){
                LabelMap[Store].insert(Label(PtrOperand, Label::LabelType::Def));
                DefLocations[PtrOperand][Func].insert(Store);
                LabelMap[Store].insert(Label(PtrOperand, Label::LabelType::Use));
                UseList[PtrOperand].insert(Store);
            }
            // store ? %0
            else if(auto Load = dyn_cast<LoadInst>(PtrOperand)){
                for(auto Pointee : PTS[Load->getPointerOperand()]){
                    LabelMap[Store].insert(Label(Pointee, Label::LabelType::Def));
                    DefLocations[Pointee][Func].insert(Store);
                    LabelMap[Store].insert(Label(Pointee, Label::LabelType::Use));
                    UseList[Pointee].insert(Store);

                }
            }
        }
        else if(auto Load = dyn_cast<LoadInst>(&Inst)){
            auto PtrOperand = Load->getPointerOperand();
            // ? = load x
            if(isa<AllocaInst>(PtrOperand)){
                LabelMap[Load].insert(Label(PtrOperand, Label::LabelType::Use));
                UseList[PtrOperand].insert(Load);

            }
            // ? = load %0
            else if(auto PtrLoad = dyn_cast<LoadInst>(PtrOperand)){
                for(auto Pointee : PTS[PtrLoad->getPointerOperand()]){
                    LabelMap[Load].insert(Label(Pointee, Label::LabelType::Use));
                    UseList[Pointee].insert(Load);
                }
            }
        }
        else if(auto Call = dyn_cast<CallInst>(&Inst)){
            for(auto Operand : Call->operand_values()){
                // call f x y
                if(isa<AllocaInst>(Operand)){
                    LabelMap[Call].insert(Label(Operand, Label::LabelType::Use));
                    UseList[Operand].insert(Call);
                    LabelMap[Call].insert(Label(Operand, Label::LabelType::Def));
                    DefLocations[Operand][Func].insert(Call);
                }
                // call f %0 %1
                else if(auto PtrLoad = dyn_cast<LoadInst>(Operand)){
                    for(auto Pointee : PTS[PtrLoad->getPointerOperand()]){
                        LabelMap[Call].insert(Label(Pointee, Label::LabelType::Use));
                        UseList[Pointee].insert(Call);
                        LabelMap[Call].insert(Label(Pointee, Label::LabelType::Def));
                        DefLocations[Pointee][Func].insert(Call);
                    }
                }
            }
        }
        else if(auto Return = dyn_cast<ReturnInst>(&Inst)){
            auto ReturnValue = Return->getReturnValue();
            
            if(ReturnValue){
                // ret x;
                if(isa<AllocaInst>(ReturnValue)){
                    LabelMap[Return].insert(Label(ReturnValue, Label::LabelType::Use));
                    UseList[ReturnValue].insert(Return);

                }
                // ret %0
                else if(auto PtrLoad = dyn_cast<LoadInst>(ReturnValue)){
                    for(auto Pointee : PTS[PtrLoad->getPointerOperand()]){
                        LabelMap[Return].insert(Label(Pointee, Label::LabelType::Use));
                        UseList[Pointee].insert(Return);
                    }
                }
            }
        }
    }

    return;
}

std::pair<std::map<const Instruction*, std::set<const Instruction*>>, DomGraph> 
    StagedFlowSensitivePointerAnalysis::buildDominatorGraph(const Function *Func, const PointerTy *Ptr){


    DomGraph DG;

    // add all instruction labeled with def(ptr) in function func to dg
    for(auto Node : DefLocations[Ptr][Func]){
        DG.addNode(Node);
    }

    // get idf

    std::set<const Instruction *> CurNodes = DG.getNodes();
    while(!CurNodes.empty()){

        std::set<const Instruction *> NextNodes{};
        for(auto Node : CurNodes){

            auto it = Func2DomFrontier.at(Func).get().find(const_cast<BasicBlock*>(Node->getParent()));
            // If a basicblock is not reachable from the entry basicblock,
            // DominanceFromtierAnalysis will not having entry for this basicblock.
            // i.e., running find will return end ietrator.
            if(it == Func2DomFrontier.at(Func).get().end()){
                CurNodes = std::set<const Instruction *>{};
                break;
            }

            for(auto DF : it->second){
                auto Ns = DG.getNodes();
                if(Ns.find(DF->getFirstNonPHIOrDbg()) == Ns.end()){
                    NextNodes.insert(DF->getFirstNonPHIOrDbg());
                }
                DG.addNode(DF->getFirstNonPHIOrDbg());
                DG.addEdge(Node, DF->getFirstNonPHIOrDbg());

            }

        }
        CurNodes = NextNodes;

    }

    // for any two nodes a b in dg, add a -> b if a dominates b or b in a's df.
    std::map<const Instruction*, std::set<const Instruction*>> Doms;
    auto Nodes = DG.getNodes();
    for(auto Node1 : Nodes){
        for(auto Node2 : Nodes){
            if(Node1 == Node2){
                continue;
            }
            if(Func2DomTree.at(Func).get().dominates(Node1, Node2)){
                Doms[Node2].insert(Node1);
            }
        }
    }

    for(auto P : Doms){
        auto IDom = *(P.second.begin());

        for(auto D : P.second){
            if(D == IDom){
                continue;
            }
            if(Func2DomTree.at(Func).get().dominates(IDom, D)){
                IDom = D;
            }
        }

        DG.addEdge(IDom, P.first);
    }


    // fix point solution in dg
    std::map<const Instruction*, std::set<const Instruction*>> IN;
    std::map<const Instruction*, std::set<const Instruction*>> OUT;

    while(true){
        bool Changed = false;
        auto Nodes = DG.getNodes();
        auto Edges = DG.getEdges();

        for(auto Node : DG.getNodes()){
            // update out
            auto OldOut = OUT[Node];
            auto AliasSet = std::set<const PointerTy *>{};
            if(AliasMap.count(Node) && AliasMap[Node].count(Ptr)){
                AliasSet = AliasMap.at(Node).at(Ptr);
            }
            if(DefLocations[Ptr][Func].find(Node) == DefLocations[Ptr][Func].end()){
                OUT[Node] = IN[Node];
            }
            else if(AliasSet.size() <= 1){
                OUT[Node] = std::set<const ProgramLocationTy*>{Node};
            }
            else{
                OUT[Node] = IN[Node];
                OUT[Node].insert(Node);
            }

            if(OldOut != OUT[Node]){
                Changed = true;
            }

            // update in
            for(auto ToNode : Edges[Node]){
                auto OldIN = IN[ToNode];
                IN[ToNode].insert(OUT[Node].begin(), OUT[Node].end());
                if(OldIN != IN[ToNode]){
                    Changed = true;
                }
            }
        }

        if(!Changed){
            break;
        }
    }

    return {OUT, DG};

}

std::set<const StagedFlowSensitivePointerAnalysis::ProgramLocationTy*> 
    StagedFlowSensitivePointerAnalysis::getUseLocations(const PointerTy *Ptr){
    if(UseList.count(Ptr)){
        return UseList.at(Ptr);
    }
    return std::set<const ProgramLocationTy*>{};
}

void StagedFlowSensitivePointerAnalysis::addDefUseEdge(const ProgramLocationTy *Def, const ProgramLocationTy *Use, const PointerTy *Ptr){

    DefUseGraph[Def][Ptr].insert(Use);
}

void StagedFlowSensitivePointerAnalysis::buildDefUseGraph(std::set<const ProgramLocationTy*> UseLocs, 
    const PointerTy *Ptr, std::map<const Instruction*, std::set<const Instruction*>> OUT, DomGraph DG){
    for(auto UseLoc : UseLocs){

        // find all def in dg that dominates useLoc
        auto Nodes = DG.getNodes();
        std::set<const ProgramLocationTy *> Dom{};
        for(auto Node : Nodes){
            // dbgs() << "NODE " << *Node << "\n";
            if(Func2DomTree.at(UseLoc->getFunction()).get().dominates(Node, UseLoc)){
                Dom.insert(Node);
                // dbgs() << "INSERT " << *Node << "\n";
            }
        }

        // find immediate dominator
        if(Dom.empty()){
            return;
        }
        auto IDom = *(Dom.begin());
        for(auto D : Dom){
            if(D == IDom){
                continue;
            }

            if(Func2DomTree.at(UseLoc->getFunction()).get().dominates(IDom, D)){
                IDom = D;
            }

        }

        // out[idom] are the defs

        auto DefLocs = OUT[IDom];
        auto it0 = Nodes.find(UseLoc);
        auto it1 = DefLocations[Ptr][UseLoc->getFunction()].find(UseLoc);
        if(it0 != Nodes.end() && it1 == DefLocations[Ptr][UseLoc->getFunction()].end()){
            DefLocs = OUT[UseLoc];
        }


        for(auto Def : DefLocs){
            addDefUseEdge(Def, UseLoc, Ptr);
        }
    }
}

std::vector<const StagedFlowSensitivePointerAnalysis::ProgramLocationTy*> StagedFlowSensitivePointerAnalysis::
    getAffectUseLocations(const ProgramLocationTy *Loc, const PointerTy *Ptr){

    std::vector<const ProgramLocationTy*> Res{};
    if(DefUseGraph.count(Loc)){
        for(auto UseLocsAndPtr : DefUseGraph.at(Loc)){
            if(Ptr == UseLocsAndPtr.first){
                Res.insert(Res.begin(), UseLocsAndPtr.second.begin(), UseLocsAndPtr.second.end());
            }
        }
    }
       
    return Res;
}

// Pointer should be all alloca ptrs and para ptrs in func. 
std::vector<StagedFlowSensitivePointerAnalysis::DefUseEdgeTupleTy> StagedFlowSensitivePointerAnalysis::
    initializePropagateList(std::set<const PointerTy*> Pointers, const Function *Func){

    std::vector<DefUseEdgeTupleTy> PropagateList{};
    for(auto Ptr: Pointers){
        if(!Func->isDeclaration()){
            if(auto Arg = dyn_cast<Argument>(Ptr)){
                auto FirstInst = Func->getEntryBlock().getFirstNonPHIOrDbg();
                auto InitialDUEdges = getAffectUseLocations(FirstInst, Arg);
                for(auto UseLoc : InitialDUEdges){
                    PropagateList.push_back(std::make_tuple(FirstInst, UseLoc, Arg));
                }
                continue;
            }
        }
        
        // An allocated pointer in llvm also represents its allocation location.
        auto Loc = dyn_cast<ProgramLocationTy>(Ptr);
        assert(Loc && "Cannot use nullptr as program location");
        auto InitialDUEdges = getAffectUseLocations(Loc, Ptr);
        for(auto UseLoc : InitialDUEdges){
            PropagateList.push_back(std::make_tuple(Loc, UseLoc, Ptr));
        }
    }
    // if(GlobalWorkList.count(PtrLvl)){
    //     for(auto Ptr : GlobalWorkList.at(PtrLvl)){
    //         // Global variables are not supported yet.
    //     }
    // }
    return PropagateList;
}

void StagedFlowSensitivePointerAnalysis::propagatePointsToInformation(const ProgramLocationTy *UseLoc,
     const ProgramLocationTy *DefLoc, const PointerTy *Ptr){

    // bug: For a store instruction store x y, if we pass pts(y) from DefLoc to
    //      UseLoc, it will overwrites existing pts(y) at UseLoc. Furthermore, we 
    //      do not really need to pass pts(y) to a store instruction since we are
    //      checking the alias set to perform which kind of points-to set
    //      update. Will remove the use label at store instruction in the future.

    // if(!dyn_cast<StoreInst>(UseLoc)){
    //     PointsToSet[UseLoc][Ptr].insert(PointsToSet.at(DefLoc).at(Ptr).begin(), PointsToSet.at(DefLoc).at(Ptr).end());
    // }
    PointsToSetIn[UseLoc][Ptr].insert(PointsToSetOut.at(DefLoc).at(Ptr).begin(), PointsToSetOut.at(DefLoc).at(Ptr).end());


    return;
}

void StagedFlowSensitivePointerAnalysis::updatePointsToSet(const ProgramLocationTy *Loc,
     const PointerTy *Pointer, std::set<const PointerTy *> AdjustedPointsToSet, 
     std::vector<DefUseEdgeTupleTy> &PropagateList){

    assert(Pointer && "Cannot update PTS for nullptr");
    auto Store = dyn_cast<StoreInst>(Loc);

    auto AliasSet = std::set<const PointerTy *>{};
    if(AliasMap.count(Loc) && AliasMap[Loc].count(Store->getPointerOperand())){
        for(auto Ptr : AliasMap.at(Loc).at(Store->getPointerOperand())){
            if(!Ptr){
                continue;
            }
            if(!isa<LoadInst>(Ptr)){
                AliasSet.insert(Ptr);
            }
        }
    }
    


    bool Changed = false;
    if(AliasSet.size() <= 1){
        // Strong update
        // dbgs() << "Strong update " << *Pointer << "\n";
        Changed = updatePointsToSetAtProgramLocation(Loc, Pointer, AdjustedPointsToSet);
    }
    else{
        // Weak update
        // dbgs() << "Weak update " << *Pointer << "\n";


        for(auto Alias : AliasSet){
            // if(!Alias){
            //     DEBUG_WITH_TYPE("fspa", dbgs() << getCurrentTime() << " WARNING: pointer " 
            //         << *Store->getPointerOperand() << " may alias to nullptr at " << *Loc << "\n");
            //         continue;
            // }
            if(dyn_cast<LoadInst>(Alias)){
                continue;
            }
            auto PTS = PointsToSetIn[Loc][Alias];
            PTS.insert(AdjustedPointsToSet.begin(), AdjustedPointsToSet.end());

            if(updatePointsToSetAtProgramLocation(Loc, Alias, PTS)){
                for(auto UseLoc : getAffectUseLocations(Loc, Alias)){    
                    PropagateList.push_back(std::make_tuple(Loc, UseLoc, Alias));
                }
            }
        }
    }

    if(Changed){
        for(auto UseLoc : getAffectUseLocations(Loc, Pointer)){    
            PropagateList.push_back(std::make_tuple(Loc, UseLoc, Pointer));
        }
    }
}

void StagedFlowSensitivePointerAnalysis::updateArgPointsToSetOfFunc(const Function *Func, std::set<const Value*> PTS, 
    size_t ArgIdx, std::vector<DefUseEdgeTupleTy> &PropagateList){
    // Densemap has no at member function in llvm-14. Move back to use std::map.

    // dbgs() << "2222222\n";

    const Value *Parameter;
    for(auto &Para : Func->args()){
        Parameter = &Para;
        if(!ArgIdx){
            break;
        }
        --ArgIdx;
    }

    
    auto FirstInst = Func->getEntryBlock().getFirstNonPHIOrDbg();

    auto OldSize = PointsToSetOut[FirstInst][Parameter].size();
    PointsToSetOut[FirstInst][Parameter].insert(PTS.begin(), PTS.end());

    if(OldSize != PointsToSetOut.at(FirstInst).at(Parameter).size()){
        for(auto UseLoc : getAffectUseLocations(FirstInst, Parameter)){    
            PropagateList.push_back(std::make_tuple(FirstInst, UseLoc, Parameter));
        }
    }
}

std::set<const StagedFlowSensitivePointerAnalysis::PointerTy*> StagedFlowSensitivePointerAnalysis::
    getRealPointsToSet(const ProgramLocationTy *Loc, const PointerTy *ValueOperand){
    
    std::set<const PointerTy*> Pointees{};

    Pointees.insert(ValueOperand->stripPointerCasts());
    if(AliasMap.count(Loc) && AliasMap[Loc].count(ValueOperand)){
        Pointees = AliasMap.at(Loc).at(ValueOperand);
    }
    
    return Pointees;
}

std::set<const StagedFlowSensitivePointerAnalysis::PointerTy*> StagedFlowSensitivePointerAnalysis::
    getAlias(const ProgramLocationTy *Loc, const LoadInst *Load){

    if(AliasMap.count(Loc) && AliasMap[Loc].count(Load->getPointerOperand())){
        return AliasMap.at(Loc).at(Load->getPointerOperand());
    }
    else{
        return std::set<const PointerTy*>{Load->getPointerOperand()};
    }
}


void StagedFlowSensitivePointerAnalysis::updateAliasInformation(const ProgramLocationTy *Loc, const LoadInst *Load){
    
    auto Aliases = getAlias(Loc, Load);
    for(auto &Alias : Aliases){
        if(!Alias){
            continue;
        }
        assert(Alias && "Cannot process nullptr");

        // if we have x = load y, and y alias z, we will need pts(z) at current program location.
        if(!AliasUser.count(Alias)){
            for(auto User : Alias->users()){
                AliasUser[Alias].insert(User);
            }
        }
        AliasUser[Alias].insert(Loc);
        if(PointsToSetOut.count(Loc) && PointsToSetOut[Loc].count(Alias)){
            AliasMap[Loc][Load].insert(PointsToSetOut.at(Loc).at(Alias).begin(), PointsToSetOut.at(Loc).at(Alias).end());
        }
    }
    return;
}

std::vector<const StagedFlowSensitivePointerAnalysis::PointerTy*> StagedFlowSensitivePointerAnalysis::
    ptsPointsTo(const ProgramLocationTy *Loc, const PointerTy *Ptr){

    std::vector<const PointerTy*> Res{};

    // if(auto Store = dyn_cast<StoreInst>(Loc)){
    //     Res.push_back(Store->getPointerOperand());
    // }


    // if(PointsToSet.count(Loc)){
    //     for(auto PtsAtPtr : PointsToSet.at(Loc)){
    //         auto it = std::find_if(PtsAtPtr.second.begin(), PtsAtPtr.second.end(), 
    //             [&](const PointerTy *pvar) -> bool {return pvar == Ptr;});
    //         if(it != PtsAtPtr.second.end()){
    //             Res.push_back(PtsAtPtr.first);
    //         }
    //     }
    // }

    if(PointsToSetOut.count(Loc)){
        for(auto PtsAtPtr : PointsToSetOut.at(Loc)){
            if(AliasMap[Loc][dyn_cast<StoreInst>(Loc)->getPointerOperand()].count(PtsAtPtr.first) || 
                PtsAtPtr.first == dyn_cast<StoreInst>(Loc)->getPointerOperand()){
                    Res.push_back(PtsAtPtr.first);
                }

        }
    }
    
    return Res;
}


void StagedFlowSensitivePointerAnalysis::updateAliasUsers(const ProgramLocationTy *Loc, 
    std::vector<DefUseEdgeTupleTy> &PropagateList){

    
    // dbgs() << AliasUser.count(Loc) << "\n";
    for(auto User : AliasUser.at(Loc)){      

        
        auto UseLoc = dyn_cast<ProgramLocationTy>(User);
        auto Ptr = dyn_cast<PointerTy>(Loc);

        if(AliasMap.count(Loc) && AliasMap[Loc].count(Ptr)){
            AliasMap[UseLoc][Ptr] = AliasMap.at(Loc).at(Ptr);
        }else{
            AliasMap[UseLoc][Ptr] = std::set<const PointerTy*>{};
        }
        
        if(auto Store = dyn_cast<StoreInst>(UseLoc)){
            if(Ptr == Store->getPointerOperand()){ 
                // if user is 'store x y', and we are passing alias-set(y), we need to make
                // pts(z) = pts(y) for each z in alias-set(y)
                
            }
            else if(Ptr == Store->getValueOperand()){
                
                auto Pointers = ptsPointsTo(UseLoc, Ptr);
                for(auto Pointer : Pointers){

                    auto OldPTS = std::set<const PointerTy*>{};
                    if(PointsToSetOut.count(UseLoc) && PointsToSetOut[UseLoc].count(Pointer)){
                        OldPTS = PointsToSetOut.at(UseLoc).at(Pointer);
                    }
                                        
                    PointsToSetOut[UseLoc][Pointer].insert(
                        AliasMap.at(UseLoc).at(Ptr).begin(), 
                        AliasMap.at(UseLoc).at(Ptr).end());


                    if(OldPTS != PointsToSetOut.at(UseLoc).at(Pointer)){

                        for(auto AffectedLoc : getAffectUseLocations(UseLoc, Pointer)){    
                                PropagateList.push_back(std::make_tuple(UseLoc, AffectedLoc, Pointer));
                        }
                    }
                }
            }
            else{
                std::string Str;
                raw_string_ostream(Str) << "Hitting at " << *Store << " with pointer " << *Loc << "\n";
                llvm_unreachable(Str.c_str());
            }
        }
        else if(auto Load = dyn_cast<LoadInst>(UseLoc)){

        }
        else if(auto Ret = dyn_cast<ReturnInst>(UseLoc)){

        }
        else if (auto Call = dyn_cast<CallInst>(UseLoc)){
            if(!Call->getCalledFunction() || Call->getCalledFunction()->isDeclaration()){
                // Ignore indirect call.
                continue;
            }

            // Find corresponding parameter index from the actual argument.
            size_t ArgumentIdx = 0;
            for(auto Arg : Call->operand_values()){
                if(Arg == Ptr){
                    break;
                }
                ++ArgumentIdx;
            }

            assert((ArgumentIdx < Call->arg_size()) && "Cannot find argument index at function.");
    
            // updateArgAliasOfFunc(Call, AliasMap.at(UseLoc).at(Ptr), ArgumentIdx, PropagateList);
        }
        else{

        }
    }
}

bool StagedFlowSensitivePointerAnalysis::updatePointsToSetAtProgramLocation(const ProgramLocationTy *Loc, 
    const PointerTy *Ptr, std::set<const PointerTy*> PTS){

        // dbgs() << "Set pts of " << *Ptr << " at " << *Loc << " to\n";
        // for(auto P : PTS){
        //     if(!P){
        //         dbgs() << "\tnullptr\n";
        //     }
        //     else{
        //         dbgs() << "\t" << *P << "\n";
        //     }
            
        // }



    auto OldPTS = std::set<const Value*>{};
    if(PointsToSetOut.count(Loc) && PointsToSetOut[Loc].count(Ptr)){
        OldPTS = PointsToSetOut.at(Loc).at(Ptr);
    }

    // dbgs() << "Old pts of " << *Ptr << " at " << *Loc << " to\n";
    // for(auto P : OldPTS){
    //     if(!P){
    //         dbgs() << "\tnullptr\n";
    //     }
    //     else{
    //         dbgs() << "\t" << *P << "\n";
    //     }
        
    // }
    
    if(OldPTS != PTS){
        PointsToSetOut[Loc][Ptr] = PTS;
        return true;
    }
    return false;
}

void StagedFlowSensitivePointerAnalysis::propagate(std::vector<DefUseEdgeTupleTy> PropagateList, 
    const Function *Func){
   
    while(!PropagateList.empty()){

        auto Edge = PropagateList.front();
        auto DefLoc = std::get<0>(Edge);
        auto UseLoc = std::get<1>(Edge);
        auto Ptr = std::get<2>(Edge);

        assert(DefLoc && "Cannot have nullptr as def loc");
        assert(UseLoc && "Cannot have nullptr as use loc");
        assert(Ptr && "Cannot have nullptr as pointer");


        propagatePointsToInformation(UseLoc, DefLoc, Ptr);

        if(auto Store = dyn_cast<StoreInst>(UseLoc)){

            auto PTS = getRealPointsToSet(UseLoc, Store->getValueOperand());
            updatePointsToSet(UseLoc, Ptr, PTS, PropagateList);
            updatePointsToSet(UseLoc, Store->getPointerOperand(), PTS, PropagateList);
        }
        else if(auto Load = dyn_cast<LoadInst>(UseLoc)){
            PointsToSetOut[UseLoc][Ptr] = PointsToSetIn.at(UseLoc).at(Ptr);
            auto OldAliasSet = std::set<const PointerTy*>{};
            if(AliasMap.count(UseLoc) && AliasMap[UseLoc].count(UseLoc)){
                OldAliasSet = AliasMap.at(UseLoc).at(UseLoc);
            }

            updateAliasInformation(UseLoc, Load);
            
            auto NewAliasSet = std::set<const PointerTy*>{};
            if(AliasMap.count(UseLoc) && AliasMap[UseLoc].count(UseLoc)){
                NewAliasSet = AliasMap.at(UseLoc).at(UseLoc);
            }

            if(OldAliasSet != NewAliasSet){
                if(!AliasUser.count(UseLoc)){
                    // Create empty entry
                    AliasUser[UseLoc];
                    for(auto user : UseLoc->users()){
                        AliasUser[UseLoc].insert(user);
                    }
                }
                updateAliasUsers(UseLoc, PropagateList);
            }
        }
        else if(auto Call = dyn_cast<CallInst>(UseLoc)){
            if(!Call->getCalledFunction() || Call->getCalledFunction()->isDeclaration()){
                // Ignore indirect call.
                PropagateList.erase(PropagateList.begin());
                continue;
            }


            // Find corresponding parameter index from the actual argument.
            size_t ArgumentIdx = 0;
            for(auto Arg : Call->operand_values()){
                if(Arg == Ptr){
                    break;
                }
                bool Stop = false;
                for(auto Alias : AliasMap[Call][Arg]){
                    if(Alias == Ptr){
                        Stop = true;
                        break;
                    }
                }
                if(Stop){
                    break;
                }
                
                ++ArgumentIdx;
            }

            if(ArgumentIdx < Call->arg_size()){
                updateArgPointsToSetOfFunc(Call->getCalledFunction(), PointsToSetIn.at(UseLoc).at(Ptr), ArgumentIdx, PropagateList);
            }
        }
        else if(auto Return = dyn_cast<ReturnInst>(UseLoc)){
            PointsToSetOut[UseLoc][Ptr] = PointsToSetIn.at(UseLoc).at(Ptr);
            if(dyn_cast<Argument>(Ptr)){
                size_t ParaIdx = 0;
                for(auto &Arg : Return->getFunction()->args()){
                    if(&Arg == Ptr){
                        break;
                    }
                    ++ParaIdx;
                }

                for(auto CallSite : Func2CallerLocation[Return->getFunction()]){
                    auto ArgIdx = ParaIdx;
                    const Value *Arg;
                    for(auto Argu : CallSite->operand_values()){
                        Arg = Argu;
                        if(!ArgIdx){
                            break;
                        }
                        --ArgIdx;
                    }

                    auto Changed = updatePointsToSetAtProgramLocation(CallSite, Arg, PointsToSetOut[Return][Ptr]);
                    if(Changed){
                        for(auto UseLoc : getAffectUseLocations(CallSite, Arg)){    
                            PropagateList.push_back(std::make_tuple(CallSite, UseLoc, Arg));
                        }
                    }
                    for(auto Alias : AliasMap[CallSite][Arg]){
                        if(updatePointsToSetAtProgramLocation(CallSite, Alias, PointsToSetOut[Return][Ptr])){
                        for(auto UseLoc : getAffectUseLocations(CallSite, Alias)){    
                            PropagateList.push_back(std::make_tuple(CallSite, UseLoc, Alias));
                        }
                    }
                    }
                }
            }
        }

        PropagateList.erase(PropagateList.begin());
    }
    return;
}


StagedFlowSensitivePointerAnalysisResult StagedFlowSensitivePointerAnalysis::run(Module &M, ModuleAnalysisManager &MAM){

    auto AndersenResult = MAM.getResult<AndersenPointerAnalysis>(M);
    auto AndersenPTS = AndersenResult.getPointsToSet();
    auto &FAM = MAM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager();

    globalInitialize(M);


    for(auto &Func : M.functions()){
        if(Func.isDeclaration()){
            continue;
        }             
        Func2DomTree.emplace(&Func, FAM.getResult<DominatorTreeAnalysis>(Func));
        Func2DomFrontier.emplace(&Func, FAM.getResult<DominanceFrontierAnalysis>(Func));
        markLabelsForFunc(&Func, AndersenPTS);
    }

    for(auto &Func : M.functions()){

        auto Pointers = std::set<const PointerTy*>{};
        if(Func2WorkList.count(&Func)){
            Pointers.insert(Func2WorkList.at(&Func).begin(), Func2WorkList.at(&Func).end());
        }
        
        for(auto Ptr : Pointers){
            // dbgs() << "Processing pointer " << *Ptr << "\n";
            auto Pair = buildDominatorGraph(&Func, Ptr);
            auto OUT = Pair.first;
            auto DG = Pair.second;
            auto UseLocs = getUseLocations(Ptr);
            buildDefUseGraph(UseLocs, Ptr, OUT, DG);
        }

        auto PropagateList = initializePropagateList(Pointers, &Func);

        // Also save caller when passing the arguments.
        propagate(PropagateList, &Func);

    }

    AnalysisResult.setPointsToSet(PointsToSetOut);

    dumpPointsToSet();


    return AnalysisResult;

}




#ifndef LLVM_TRANSFORM_STAGED_FLOW_SENSITIVE_POINTER_ANALYSIS_H
#define LLVM_TRANSFORM_STAGED_FLOW_SENSITIVE_POINTER_ANALYSIS_H

#include "llvm/IR/PassManager.h"
#include "llvm/Transforms/Utils/FlowSensitivePointerAnalysis.h"


#include <map>
#include <set>


namespace llvm{

    class StagedFlowSensitivePointerAnalysisResult{

        using PointerTy = Value;
        using ProgramLocationTy = Instruction;
        using PointsToSetTy = std::map<const ProgramLocationTy*, std::map<const PointerTy*, std::set<const PointerTy*>>>;


        PointsToSetTy PointsToSet;


        public:
            PointsToSetTy getPointsToSet(){
                return PointsToSet;
            }
            void setPointsToSet(PointsToSetTy PTS){
                PointsToSet = PTS;
            }


    };

    class StagedFlowSensitivePointerAnalysis : public AnalysisInfoMixin<StagedFlowSensitivePointerAnalysis>{

        using PointerTy = Value;
        using ProgramLocationTy = Instruction;
        using WorkListTy = std::set<const PointerTy*>;
        using PointsToSetTy = std::map<const ProgramLocationTy*, std::map<const PointerTy*, std::set<const PointerTy*>>>;
        using DefUseEdgeTupleTy = std::tuple<const ProgramLocationTy*, const ProgramLocationTy*, const PointerTy*>;
        using DefUseGraphTy = std::map<const ProgramLocationTy*, std::map<const PointerTy*, std::set<const ProgramLocationTy*>>>;




        friend AnalysisInfoMixin<StagedFlowSensitivePointerAnalysis>;
        static AnalysisKey Key;
        static bool isRequired() { return true; }



        StagedFlowSensitivePointerAnalysisResult AnalysisResult;
        std::map<const ProgramLocationTy*, std::set<Label>> LabelMap;
        PointsToSetTy PointsToSetOut;
        PointsToSetTy PointsToSetIn;
        std::map<const Function*, std::set<const ProgramLocationTy*>> Func2CallerLocation;
        std::map<const Function*, std::set<const BasicBlock*>> Func2TerminateBBs;
        std::map<const Value*, std::set<const ProgramLocationTy*>> UseList;
        std::map<const Function*, WorkListTy> Func2WorkList; 
        PointsToSetTy AliasMap;
        std::map<const PointerTy*, std::set<const User*>> AliasUser;
        std::map<const PointerTy*, std::map<const Function*, std::set<const ProgramLocationTy*>>> DefLocations;
        std::map<const Function*, std::reference_wrapper<DominatorTreeAnalysis::Result>> Func2DomTree;
        std::map<const Function*, std::reference_wrapper<DominanceFrontierAnalysis::Result>> Func2DomFrontier;
        DefUseGraphTy DefUseGraph;






        std::pair<std::map<const Instruction*, std::set<const Instruction*>>, DomGraph> 
            buildDominatorGraph(const Function *Func, const PointerTy *Ptr);
        std::set<const ProgramLocationTy*> getUseLocations(const PointerTy *Ptr);
        void buildDefUseGraph(std::set<const ProgramLocationTy*> UseLocs, 
            const PointerTy *Ptr, std::map<const Instruction*, std::set<const Instruction*>> OUT, DomGraph DG);
        std::vector<DefUseEdgeTupleTy> initializePropagateList(
            std::set<const PointerTy*> Pointers, const Function *Func);
        void propagate(std::vector<DefUseEdgeTupleTy> PropagateList, const Function *Func);
        void propagatePointsToInformation(const ProgramLocationTy *UseLoc,
            const ProgramLocationTy *DefLoc, const PointerTy *Ptr);
        void updatePointsToSet(const ProgramLocationTy *Loc, const PointerTy *Pointer, 
            std::set<const PointerTy *> AdjustedPointsToSet, std::vector<DefUseEdgeTupleTy> &PropagateList, bool IsStrongUpdate = true);
        void updateArgPointsToSetOfFunc(const Function *Func, std::set<const Value*> PTS, 
            size_t ArgIdx, std::vector<DefUseEdgeTupleTy> &PropagateList);
        std::vector<const ProgramLocationTy*> getAffectUseLocations(const ProgramLocationTy *Loc, const PointerTy *Ptr);
        std::set<const PointerTy*> getRealPointsToSet(const ProgramLocationTy *Loc, const PointerTy *ValueOperand);
        void updateAliasInformation(const ProgramLocationTy *Loc, const LoadInst *Load);
        void updateAliasUsers(const ProgramLocationTy *Loc, std::vector<DefUseEdgeTupleTy> &PropagateList);
        bool updatePointsToSetAtProgramLocation(const ProgramLocationTy *Loc, const PointerTy *Ptr, std::set<const PointerTy*> PTS);
        void globalInitialize(Module&);
        void initialize(const Function*);
        void markLabelsForFunc(const Function*, std::map<const Value *, std::set<const Value *>>&);
        void addDefUseEdge(const ProgramLocationTy *Def, const ProgramLocationTy *Use, const PointerTy *Ptr);
        std::set<const PointerTy*> getAlias(const ProgramLocationTy *Loc, const LoadInst *Load);
        std::vector<const PointerTy*> ptsPointsTo(const ProgramLocationTy *Loc, const PointerTy *Ptr);
        void printPointsToSetAtProgramLocation(const ProgramLocationTy *Loc);
        void dumpPointsToSet();
        void dumpAliasMap();
        void updateArgAliasOfFunc(const CallInst *CallSite, std::set<const PointerTy*> AliasSet, size_t ArgIdx,
        std::vector<DefUseEdgeTupleTy> &PropagateList);




        public:
            using Result = StagedFlowSensitivePointerAnalysisResult;
            StagedFlowSensitivePointerAnalysisResult run(Module&, ModuleAnalysisManager&);
            StagedFlowSensitivePointerAnalysisResult getResult() {return AnalysisResult;}


    };


}   //namespace llvm





#endif
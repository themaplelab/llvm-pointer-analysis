#ifndef LLVM_TRANSFORM_FLOW_SENSITIVE_POINTER_ANALYSIS_H
#define LLVM_TRANSFORM_FLOW_SENSITIVE_POINTER_ANALYSIS_H

#include "llvm/ADT/BreadthFirstIterator.h"
#include "llvm/ADT/DepthFirstIterator.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/Analysis/MemoryLocation.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/WithColor.h"
#include <map>
#include <new>
#include <set>
#include <stack>
#include <utility>
#include <vector>







#define LLVM_TRANSFORM_FLOW_SENSITIVE_POINTER_ANALYSIS_ANALYSIS


/*
    Run interprocedural pointer analysis on LLVM module. The module should contain all related source code linked with
    llvm-link. 

    Potential bug:
        For some LLVM installation, we need -DNDEBUG to enable traversing CallGraph.
*/

namespace llvm{

    /// @brief Class that keeps result of flow sensitive pointer analysis
    class FlowSensitivePointerAnalysisResult{
        std::map<const Function*, std::map<size_t, std::set<const Value*>>> worklist;
        std::map<const Instruction*, std::map<const Value*, std::pair<std::set<const Value*>, bool>>> pointsToSet;
        std::map<const Function*, SetVector<const Value*>> Func2AllocatedPointersAndParameterAliases;



        public:
            std::map<const Function*, std::map<size_t, std::set<const Value*>>> getWorkList() {return worklist;}
            void setWorkList(std::map<const Function*, std::map<size_t, std::set<const Value*>>> wl) {worklist = wl; return;}
            std::map<const Instruction*, std::map<const Value*, std::pair<std::set<const Value*>, bool>>> getPointsToSet(){
                return pointsToSet;
            }
            void setPointsToSet(std::map<const Instruction*, std::map<const Value*, std::pair<std::set<const Value*>, bool>>> pts){
                pointsToSet = pts;
            }

            std::map<const Function*, SetVector<const Value*>> getFunc2Pointers() {return Func2AllocatedPointersAndParameterAliases;}
            void setFunc2Pointers(std::map<const Function*, SetVector<const Value*>> F2P){
                Func2AllocatedPointersAndParameterAliases = F2P;
                return;
            }
            

    };

    struct Label;


    class FlowSensitivePointerAnalysis : public AnalysisInfoMixin<FlowSensitivePointerAnalysis>{




        friend AnalysisInfoMixin<FlowSensitivePointerAnalysis>;
        using PointerTy = Value;
        using ProgramLocationTy = Instruction;
        using DefUseEdgeTupleTy = std::tuple<const ProgramLocationTy*, const ProgramLocationTy*, const PointerTy*>;
        using PointsToSetTy = std::map<const ProgramLocationTy*, std::map<const PointerTy*, std::pair<std::set<const PointerTy*>, bool>>>;

        std::map<const Function*, std::map<size_t, std::set<const PointerTy*>>> func2worklist;
        std::map<size_t, std::set<const Instruction*>> globalWorkList;
        std::map<const Value*, std::set<const ProgramLocationTy *>> useList;
        std::map<const Function*, std::set<const ProgramLocationTy *>> func2CallerLocation;
        std::map<const Function*, SetVector<const PointerTy*>> Func2AllocatedPointersAndParameterAliases;

        std::unique_ptr<CallGraph> cg;
        std::map<const Function *, bool> visited; 
        // How to represent a points-to set?
        PointsToSetTy pointsToSet;
        std::map<const Instruction*, MemoryLocation> memoryLocationMap;
        std::map<const Instruction*, std::set<Label>> labelMap; 
        // std::map<size_t, std::set<const Instruction*>> worklist;
        std::map<const Value*, std::set<const ProgramLocationTy*>> useLocations;
        std::map<const ProgramLocationTy*, std::map<const PointerTy*, std::set<const ProgramLocationTy*>>> defUseGraph;
        std::map<const Instruction*, std::map<const Value*, std::set<const Value *>>> aliasMap;
        std::stack<const Function*> left2Analysis;
        // Map each pointer to the program location that requires its alias information.
        std::map<const PointerTy*, std::set<const User*>> aliasUser;

        

        size_t TotalFunctionNumber = 0;
        size_t ProcessedFunctionNumber = 0;



        FlowSensitivePointerAnalysisResult result;

        static AnalysisKey Key;
        static bool isRequired() { return true; }

        private:
            // const Function* getFunctionInCallGrpahByName(std::string name);
            void getCallGraphFromModule(Module &m){
                cg = std::unique_ptr<CallGraph>(new CallGraph(m));
            }
            // size_t countPointerLevel(const AllocaInst *allocaInst);
            void initialize(const Function * const func);


            // todo: move intra-procedural pointer analysis here.
            // At each call site, do recursive call on the callee.
            void performPointerAnalysisOnFunction(const Function *func, size_t ptrLvl);
            std::set<const Instruction*> FindDefInBasicBlock(const ProgramLocationTy *loc, const PointerTy *ptr, std::set<const BasicBlock*> &visited);
            bool hasDef(const ProgramLocationTy *loc, const PointerTy *ptr);
            std::set<const Instruction*> findDefFromBB(const BasicBlock *bb, const PointerTy *p, std::set<const BasicBlock*> &visited);
            // std::vector<const Instruction*> getDUEdgesOfPtrAtClause(std::map<const Instruction*, std::set<const Instruction *>> u2p, const Instruction *ptr);
            std::vector<const ProgramLocationTy*> getAffectUseLocations(const ProgramLocationTy *loc, const Value *ptr);
            
            void propagatePointsToInformation(const ProgramLocationTy *t, const ProgramLocationTy *f, const PointerTy *pvar);
            std::set<const Value*> getRealPointsToSet(const ProgramLocationTy *t, const Value *ValueOperand);
            void updateAliasInformation(const ProgramLocationTy *t, const LoadInst *pt);
            std::set<const Value*> getAlias(const ProgramLocationTy *t, const Instruction *p);
            std::vector<const Value*> ptsPointsTo(const Instruction *user, const Instruction *t);
            // bool notVisited(const Function *f);
            // std::vector<const Function*> collectAllCallees(const Function*);
            void addDefUseEdge(const ProgramLocationTy *def, const ProgramLocationTy *use, const PointerTy *ptr);
            void updatePointsToSet(const ProgramLocationTy *loc, const Value *ptr, std::set<const Value *> pointsToSet, std::vector<DefUseEdgeTupleTy> &propagateList);

            void markLabelsForPtr(const PointerTy*);
            void buildDefUseGraph(std::set<const ProgramLocationTy*>, const PointerTy*);
            std::vector<DefUseEdgeTupleTy> initializePropagateList(std::set<const PointerTy *>, size_t);
            void propagate(std::vector<DefUseEdgeTupleTy> pl, const Function *Func);
            bool updateArgAliasOfFunc(const Function*, std::set<const Value *>, size_t);
            bool updateArgPtsofFunc(const Function*, const PointerTy*, std::set<const Value*>);
            size_t globalInitialize(Module &m);
            void processGlobalVariables(int ptrLvl);
            std::set<const ProgramLocationTy*> getUseLocations(const PointerTy*);
            void updateAliasUsers(std::set<const User *> users, const ProgramLocationTy *t, std::vector<DefUseEdgeTupleTy> &propagateList);
            size_t computePointerLevel(const Instruction *inst);
            std::set<const Instruction*> findDefFromFunc(const Function *func, const PointerTy *ptr, std::set<const BasicBlock*> &visited);
            std::set<const Function*> getCallees(const Function *func);
            std::set<const Instruction*> FindDefFromPrevOfUseLoc(const ProgramLocationTy*, const PointerTy*);
            std::set<const Instruction*> FindDefFromUseLoc(const ProgramLocationTy*, const PointerTy*, std::set<const ProgramLocationTy*> &);
            void dumpPointsToSet();
            void dumpLabelMap();
            bool updatePointsToSetAtProgramLocation(const ProgramLocationTy*, const PointerTy*, std::set<const Value*>);
            void printPointsToSetAtProgramLocation(const ProgramLocationTy *Loc);
            std::set<const Instruction*> getPrevProgramLocations(const ProgramLocationTy *Loc, bool skip = false);
            void populatePointsToSet(Module &m);
            // std::set<const PointerTy*> populatePointsToSetFromProgramLocation(const ProgramLocationTy *Loc, const PointerTy *p, std::set<const ProgramLocationTy*> &Visited, const DenseSet<const PointerTy*> &AllocatedPointers);
            void populatePTSAtLocation(const ProgramLocationTy *Loc, std::map<const PointerTy *, std::set<const PointerTy *>> PassedPTS, DenseSet<const ProgramLocationTy*> &Visited);
            



            std::map<const Function*, std::map<const Value*, std::set<const Value*>>> funcParas2AliasSet;
            std::map<const Function*, std::map<const Value*, std::set<const Value*>>> funcParas2PointsToSet;
            std::map<const Function*, std::set<const BasicBlock *>> func2TerminateBBs;
            std::map<const Function*, std::set<const Function *>> caller2Callee;
            // useLoc => {ptr => {defLocs}}
            std::map<const ProgramLocationTy*, std::map<const PointerTy*, std::set<const ProgramLocationTy*>>> DefLoc;
            WithColor logger = WithColor(outs(), HighlightColor::String);



        public:
            using Result = FlowSensitivePointerAnalysisResult;
            #ifdef LLVM_TRANSFORM_FLOW_SENSITIVE_POINTER_ANALYSIS_ANALYSIS
                FlowSensitivePointerAnalysisResult run(Module &m, ModuleAnalysisManager &mam);
            #else
                PreservedAnalyses run(Module &m, ModuleAnalysisManager &mam);
            #endif
            FlowSensitivePointerAnalysisResult getResult() {return result;}
    };


    // Since we need to create labels before creating def-use edge, we need to associate an instruction to a series of labels.
    // This class represents a single label. As a label, it records:
    //      1. whether this is a def or use or def-use.
    //      2. the memoryobject being defed or used. 
    struct Label{

        const Value *ptr;
        enum class LabelType{
            None = 0, Use, Def, DefUse
        };
        LabelType type;

        // Label() = default;
        Label(const Value *p, Label::LabelType tp) : ptr(p), type(tp) {}
    };

    raw_ostream& operator<<(raw_ostream &os, const Label &l);
    bool operator<(const Label &l1, const Label &l2);

    raw_ostream& operator<<(raw_ostream &os, const std::map<const Instruction*, std::map<const Instruction*, std::pair<std::set<const Value*>, bool>>> &pts);
    raw_ostream& operator<<(raw_ostream &os, const std::map<size_t, std::set<const Instruction *>> &wl);
    raw_ostream& operator<<(raw_ostream &os, const std::vector<const Instruction *> &l);
    raw_ostream& operator<<(raw_ostream &os, const std::map<const Instruction*, std::map<const Instruction*, std::set<const Instruction*>>> &l);


} //namespace llvm



#endif //LLVM_TRANSFORM_FLOW_SENSITIVE_POINTER_ANALYSIS_H
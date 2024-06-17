#ifndef LLVM_TRANSFORM_FLOW_SENSITIVE_POINTER_ANALYSIS_H
#define LLVM_TRANSFORM_FLOW_SENSITIVE_POINTER_ANALYSIS_H

#include "llvm/Analysis/CallGraph.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Debug.h"
#include "llvm/ADT/DepthFirstIterator.h"
#include "llvm/ADT/BreadthFirstIterator.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/Analysis/MemoryLocation.h"
#include "llvm/IR/Instruction.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/WithColor.h"
#include <utility>
#include <vector>
#include <new>
#include <map>
#include <set>
#include <stack>
#include "llvm/IR/PassManager.h"


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
        DenseMap<const Function*, DenseMap<size_t, DenseSet<const Instruction*>>> worklist;
        std::map<const Instruction*, std::map<const Instruction*, std::pair<DenseSet<const Value*>, bool>>> pointsToSet;


        public:
            DenseMap<const Function*, DenseMap<size_t, DenseSet<const Instruction*>>> getWorkList() {return worklist;}
            void setWorkList(DenseMap<const Function*, DenseMap<size_t, DenseSet<const Instruction*>>> wl) {worklist = wl; return;}
            std::map<const Instruction*, std::map<const Instruction*, std::pair<DenseSet<const Value*>, bool>>> getPointsToSet(){
                return pointsToSet;
            }
            void setPointsToSet(std::map<const Instruction*, std::map<const Instruction*, std::pair<DenseSet<const Value*>, bool>>> pts){
                pointsToSet = pts;
            }
            

    };

    struct Label;


    class FlowSensitivePointerAnalysis : public AnalysisInfoMixin<FlowSensitivePointerAnalysis>{




        friend AnalysisInfoMixin<FlowSensitivePointerAnalysis>;
        using PointerTy = Instruction;
        using ProgramLocationTy = Instruction;
        using DefUseEdgeTupleTy = std::tuple<const ProgramLocationTy*, const ProgramLocationTy*, const PointerTy*>;

        DenseMap<const Function*, DenseMap<size_t, DenseSet<const Instruction*>>> func2worklist;
        DenseMap<size_t, DenseSet<const Instruction*>> globalWorkList;
        DenseMap<const Value*, DenseSet<const ProgramLocationTy *>> useList;
        DenseMap<const Function*, DenseSet<const ProgramLocationTy *>> func2CallerLocation;

        std::unique_ptr<CallGraph> cg;
        DenseMap<const Function *, bool> visited; 
        // How to represent a points-to set?
        std::map<const Instruction*, std::map<const Instruction*, std::pair<DenseSet<const Value*>, bool>>> pointsToSet;
        DenseMap<const Instruction*, MemoryLocation> memoryLocationMap;
        std::map<const Instruction*, std::set<Label>> labelMap; 
        // DenseMap<size_t, DenseSet<const Instruction*>> worklist;
        DenseMap<const Value*, DenseSet<const ProgramLocationTy*>> useLocations;
        std::map<const ProgramLocationTy*, std::map<const ProgramLocationTy*, std::set<const PointerTy*>>> defUseGraph;
        std::map<const Instruction*, std::map<const Value*, DenseSet<const Value *>>> aliasMap;
        std::stack<const Function*> left2Analysis;
        // Map each pointer to the program location that requires its alias information.
        std::map<const Instruction*, std::set<const User*>> aliasUser;
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
            DenseSet<const Instruction*> FindDefInBasicBlock(const ProgramLocationTy *loc, const PointerTy *ptr);
            bool hasDef(const ProgramLocationTy *loc, const PointerTy *ptr);
            DenseSet<const Instruction*> findDefFromBB(const BasicBlock *bb, const PointerTy *p, DenseSet<const BasicBlock*> visited);
            // std::vector<const Instruction*> getDUEdgesOfPtrAtClause(std::map<const Instruction*, std::set<const Instruction *>> u2p, const Instruction *ptr);
            std::vector<const ProgramLocationTy*> getAffectUseLocations(const ProgramLocationTy *loc, const PointerTy *ptr);
            
            void propagatePointsToInformation(const ProgramLocationTy *t, const ProgramLocationTy *f, const PointerTy *pvar);
            DenseSet<const Value*> calculatePointsToInformationForStoreInst(const ProgramLocationTy *t, const StoreInst *pt, DenseMap<const Value*, DenseSet<const Value*>> para2Alias);
            void updateAliasInformation(const ProgramLocationTy *t, const LoadInst *pt);
            DenseSet<const Value*> getAlias(const ProgramLocationTy *t, const Instruction *p);
            std::vector<const Value*> ptsPointsTo(const Instruction *user, const Instruction *t);
            // bool notVisited(const Function *f);
            // std::vector<const Function*> collectAllCallees(const Function*);
            void addDefUseEdge(const ProgramLocationTy *def, const ProgramLocationTy *use, const PointerTy *ptr);
            void updatePointsToSet(const ProgramLocationTy *loc, const PointerTy *ptr, DenseSet<const Value *> pointsToSet, std::vector<DefUseEdgeTupleTy> &propagateList);
            
            void dumpWorkList();
            void dumpLabelMap();
            void dumpDefUseGraph() const;
            void dumpPointsToMap();

            DenseSet<const ProgramLocationTy*> markLabelsForPtr(const PointerTy*);
            void buildDefUseGraph(DenseSet<const ProgramLocationTy*>, const PointerTy*);
            std::vector<DefUseEdgeTupleTy> initializePropagateList(DenseSet<const PointerTy *>, size_t);
            void propagate(std::vector<DefUseEdgeTupleTy> pl, DenseMap<const Value*, DenseSet<const Value*>> para2Alias, DenseMap<const Value*, DenseSet<const Value*>> para2Pts);
            bool updateArgAliasOfFunc(const Function*, DenseSet<const Value *>, size_t);
            bool updateArgPtsofFunc(const Function*, const PointerTy*, DenseSet<const Value*>);
            size_t globalInitialize(Module &m);
            void processGlobalVariables(int ptrLvl);
            DenseSet<const ProgramLocationTy*> getUseLocations(const PointerTy*);
            void updateAliasUsers(std::set<const User *> users, const ProgramLocationTy *t, std::vector<DefUseEdgeTupleTy> &propagateList);
            DenseMap<size_t, DenseSet<const Instruction*>> computePointerLevel(DenseSet<std::pair<const Value*, const Value*>> constraints, DenseMap<const Value *, int> pointers);
            DenseSet<const Instruction*> findDefFromFunc(const Function *func, const PointerTy *ptr);
            DenseSet<const Function*> getCallees(const Function *func);

            DenseMap<const Function*, DenseMap<const Value*, DenseSet<const Value*>>> funcParas2AliasSet;
            DenseMap<const Function*, DenseMap<const Value*, DenseSet<const Value*>>> funcParas2PointsToSet;
            DenseMap<const Function*, DenseSet<const BasicBlock *>> func2TerminateBBs;
            DenseMap<const Function*, DenseSet<const Function *>> caller2Callee;
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
    raw_ostream& operator<<(raw_ostream &os, const DenseMap<size_t, DenseSet<const Instruction *>> &wl);
    raw_ostream& operator<<(raw_ostream &os, const std::vector<const Instruction *> &l);
    raw_ostream& operator<<(raw_ostream &os, const std::map<const Instruction*, std::map<const Instruction*, std::set<const Instruction*>>> &l);


} //namespace llvm



#endif //LLVM_TRANSFORM_FLOW_SENSITIVE_POINTER_ANALYSIS_H
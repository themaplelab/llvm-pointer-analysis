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
#include <utility>
#include <vector>
#include <new>
#include <map>
#include <set>
#include <stack>
#include "llvm/IR/PassManager.h"

/*
    Run interprocedural pointer analysis on LLVM module. The module should contain all related source code linked with
    llvm-link. 

    Potential bug:
        For some LLVM installation, we need -DNDEBUG to enable traversing CallGraph.
*/

namespace llvm{

    class FlowSensitivePointerAnalysisResult{
        DenseMap<size_t, DenseSet<const Instruction*>> worklist;
        std::map<const Instruction*, std::map<const Instruction*, std::pair<std::set<const Value*>, bool>>> pointsToSet;


        public:
            DenseMap<size_t, DenseSet<const Instruction*>> getWorkList() {return worklist;}
            void setWorkList(DenseMap<size_t, DenseSet<const Instruction*>> wl) {worklist = wl; return;}
            std::map<const Instruction*, std::map<const Instruction*, std::pair<std::set<const Value*>, bool>>> getPointsToSet(){
                return pointsToSet;
            }
            void setPointsToSet(std::map<const Instruction*, std::map<const Instruction*, std::pair<std::set<const Value*>, bool>>> pts){
                pointsToSet = pts;
            }
            

    };

    struct Label;


    class FlowSensitivePointerAnalysis : public AnalysisInfoMixin<FlowSensitivePointerAnalysis>{
        friend AnalysisInfoMixin<FlowSensitivePointerAnalysis>;
        using PointerTy = Instruction;
        using ProgramLocationTy = Instruction;
        using DefUseEdgeTupleTy = std::tuple<const ProgramLocationTy*, const ProgramLocationTy*, const PointerTy*>;

        std::unique_ptr<CallGraph> cg;
        DenseMap<const Function *, bool> visited; 
        // How to represent a points-to set?
        std::map<const Instruction*, std::map<const Instruction*, std::pair<std::set<const Value*>, bool>>> pointsToSet;
        DenseMap<const Instruction*, MemoryLocation> memoryLocationMap;
        std::map<const Instruction*, std::set<Label>> labelMap; 
        DenseMap<size_t, DenseSet<const Instruction*>> worklist;
        std::map<const Value*, std::vector<const Instruction*>> useList;
        std::map<const ProgramLocationTy*, std::map<const ProgramLocationTy*, std::set<const PointerTy*>>> defUseGraph;
        std::map<const Instruction*, std::map<const Value*, std::set<const Value *>>> aliasMap;
        std::stack<const Function*> left2Analysis;
        // Map each pointer to the program location that requires its alias information.
        std::map<const Instruction*, std::set<const User*>> aliasUser;
        FlowSensitivePointerAnalysisResult result;

        static AnalysisKey Key;
        static bool isRequired() { return true; }

        private:
            const Function* getFunctionInCallGrpahByName(std::string name);
            void getCallGraphFromModule(Module &m){
                cg = std::unique_ptr<CallGraph>(new CallGraph(m));
            }
            // size_t countPointerLevel(const AllocaInst *allocaInst);
            void initialize(const Function * const func);


            // todo: move intra-procedural pointer analysis here.
            // At each call site, do recursive call on the callee.
            void performPointerAnalysisOnFunction(const Function* const func);
            void propagate(size_t currentPtrLvl, const Function* func);
            std::vector<const Instruction*> findDefFromUse(const ProgramLocationTy *loc, const PointerTy *ptr);
            bool hasDef(const ProgramLocationTy *loc, const PointerTy *ptr);
            std::vector<const Instruction*> findDefFromBB(const BasicBlock *bb, const PointerTy *p);
            // std::vector<const Instruction*> getDUEdgesOfPtrAtClause(std::map<const Instruction*, std::set<const Instruction *>> u2p, const Instruction *ptr);
            std::vector<const ProgramLocationTy*> getAffectUseLocations(const ProgramLocationTy *loc, const PointerTy *ptr);
            
            void propagatePointsToInformation(const ProgramLocationTy *t, const ProgramLocationTy *f, const PointerTy *pvar);
            std::set<const Value*> calculatePointsToInformationForStoreInst(const ProgramLocationTy *t, const StoreInst *pt);
            void updateAliasInformation(const ProgramLocationTy *t, const LoadInst *pt);
            std::set<const Value*> getAlias(const Instruction *t, const Instruction *p);
            std::vector<const Value*> ptsPointsTo(const Instruction *user, const Instruction *t);
            // bool notVisited(const Function *f);
            // std::vector<const Function*> collectAllCallees(const Function*);
            void addDefUseEdge(const ProgramLocationTy *def, const ProgramLocationTy *use, const PointerTy *ptr);
            void updatePointsToSet(const ProgramLocationTy *loc, const PointerTy *ptr, std::set<const Value *> pointsToSet, std::vector<DefUseEdgeTupleTy> &propagateList);
            
            void dumpWorkList();
            void dumpLabelMap();
            void dumpDefUseGraph() const;
            void dumpPointsToMap();


        public:
            using Result = FlowSensitivePointerAnalysisResult;

            FlowSensitivePointerAnalysisResult run(Module &m, ModuleAnalysisManager &mam);
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
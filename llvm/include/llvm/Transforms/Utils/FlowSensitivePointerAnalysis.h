#ifndef LLVM_TRANSFORM_FLOW_SENSITIVE_POINTER_ANALYSIS_H
#define LLVM_TRANSFORM_FLOW_SENSITIVE_POINTER_ANALYSIS_H

#include "llvm/ADT/BreadthFirstIterator.h"
#include "llvm/ADT/DepthFirstIterator.h"
#include "llvm/ADT/DirectedGraph.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/Analysis/DominanceFrontier.h"
#include "llvm/Analysis/MemoryLocation.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/WithColor.h"
#include <functional>
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

    class DomGraph{
        public:
            DomGraph() = default;
            ~DomGraph(){};

            void addNode(const Instruction *Node){
                Nodes.insert(Node);
            }

            std::set<const Instruction *> getNodes(){
                return Nodes;
            }

            void addEdge(const Instruction *From,  const Instruction *To){
                if(Nodes.find(From) == Nodes.end()){
                    dbgs() << "Node " << *From << "not in node list\n";
                    return;
                }
                if(Nodes.find(To) == Nodes.end()){
                    dbgs() << "Node " << *To << "not in node list\n";
                    return;
                }
                Edges[From].insert(To);
            }

            std::map<const Instruction *, std::set<const Instruction *>> getEdges(){
                return Edges;
            }


        private:
            const Instruction *Root;
            std::set<const Instruction *> Nodes;
            std::map<const Instruction *, std::set<const Instruction *>> Edges;
    };

    /// @brief Class that keeps result of flow sensitive pointer analysis
    class FlowSensitivePointerAnalysisResult{

        using PointerTy = Value;
        using ProgramLocationTy = Instruction;
        using WorkListTy = std::map<size_t, std::set<const PointerTy*>>;
        using PointsToSetTy = std::map<const ProgramLocationTy*, std::map<const PointerTy*, std::set<const PointerTy*>>>;


        std::map<const Function*, WorkListTy> Worklist;
        PointsToSetTy PointsToSet;
        std::map<const Function*, SetVector<const Value*>> Func2AllocatedPointersAndParameterAliases;



        public:
            std::map<const Function*, WorkListTy> getWorkList() {return Worklist;}
            void setWorkList(std::map<const Function*, WorkListTy> WL) {Worklist = WL; return;}
            PointsToSetTy getPointsToSet(){
                return PointsToSet;
            }
            void setPointsToSet(PointsToSetTy PTS){
                PointsToSet = PTS;
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
        friend Label;

        using PointerTy = Value;
        using ProgramLocationTy = Instruction;
        using PointsToSetTy = std::map<const ProgramLocationTy*, std::map<const PointerTy*, std::set<const PointerTy*>>>;
        using WorkListTy = std::map<size_t, std::set<const PointerTy*>>;
        using DefUseEdgeTupleTy = std::tuple<const ProgramLocationTy*, const ProgramLocationTy*, const PointerTy*>;
        using DefUseGraphTy = std::map<const ProgramLocationTy*, std::map<const PointerTy*, std::set<const ProgramLocationTy*>>>;

        // Map each pointer to the program location that requires its alias information.
        PointsToSetTy AliasMap;
        std::map<const PointerTy*, std::set<const User*>> AliasUser;
        std::map<const Function*, std::set<const Function*>> Caller2Callee;
        DefUseGraphTy DefUseGraph;
        std::map<const Function*, SetVector<const PointerTy*>> Func2AllocatedPointersAndParameterAliases;
        std::map<const Function*, std::set<const ProgramLocationTy*>> Func2CallerLocation;
        std::map<const Function*, WorkListTy> Func2WorkList; 
        std::map<const Function*, std::set<const BasicBlock*>> Func2TerminateBBs;
        std::map<const Function*, PointsToSetTy::mapped_type> FuncParas2PointsToSet;
        WorkListTy GlobalWorkList;
        std::map<const ProgramLocationTy*, std::set<Label>> LabelMap; 
        PointsToSetTy PointsToSetOut;
        PointsToSetTy PointsToSetIn;
        std::map<const Value*, std::set<const ProgramLocationTy*>> UseList;
        std::map<const Function*, std::reference_wrapper<DominatorTreeAnalysis::Result>> Func2DomTree;
        std::map<const Function*, std::reference_wrapper<DominanceFrontierAnalysis::Result>> Func2DomFrontier;
        std::map<const PointerTy*, std::map<const Function*, std::set<const ProgramLocationTy*>>> DefLocations;
        std::map<const CallInst*, std::map<const PointerTy*, std::set<size_t>>> CallSite2ArgIdx;

        FlowSensitivePointerAnalysisResult AnalysisResult;

        static AnalysisKey Key;
        static bool isRequired() { return true; }

        private:
            void addDefUseEdge(const ProgramLocationTy*, const ProgramLocationTy*, const PointerTy*);
            void addDefLabel(const PointerTy *Ptr, const ProgramLocationTy *Loc, const Function *Func);
            void addUseLabel(const PointerTy *Ptr, const ProgramLocationTy *Loc);
            std::pair<std::map<const Instruction*, std::set<const Instruction*>>, DomGraph> 
                buildDominatorGraph(const Function *Func, const PointerTy *Ptr);
            void buildDefUseGraph(std::set<const ProgramLocationTy*>, const PointerTy*, 
                std::map<const Instruction*, std::set<const Instruction*>>, DomGraph);
            size_t computePointerLevel(const PointerTy*);
            void dumpAliasMap();
            void dumpLabelMap();
            void dumpPointsToSet();
            std::vector<const ProgramLocationTy*> getAffectUseLocations(const ProgramLocationTy*, const PointerTy*);
            std::set<const PointerTy*> getAlias(const ProgramLocationTy*, const LoadInst*);
            std::set<const PointerTy*> getRealPointsToSet(const ProgramLocationTy*, const PointerTy*);
            std::set<const ProgramLocationTy*> getUseLocations(const PointerTy*);
            size_t globalInitialize(Module&);
            bool hasDef(const ProgramLocationTy*, const PointerTy*);
            size_t initialize(const Function*);
            SetVector<DefUseEdgeTupleTy> initializePropagateList(std::set<const PointerTy*>, size_t, const Function *);
            bool insertPointsToSetAtProgramLocation(const ProgramLocationTy *, const PointerTy *, std::set<const PointerTy*>&);
            void markLabelsForPtr(const PointerTy*);
            void printPointsToSetAtProgramLocation(const ProgramLocationTy*);
            void processGlobalVariables(size_t);
            void propagate(SetVector<DefUseEdgeTupleTy>, const Function*);
            void propagatePointsToInformation(const ProgramLocationTy*, const ProgramLocationTy*, const PointerTy*);
            std::vector<const PointerTy*> ptsPointsTo(const ProgramLocationTy*, const PointerTy*);
            void updateAliasInformation(const ProgramLocationTy *, const LoadInst *);
            void updateAliasUsers(const ProgramLocationTy*, SetVector<DefUseEdgeTupleTy>&);
            void updateArgPointsToSetOfFunc(const Function*, std::set<const PointerTy*>, size_t, SetVector<DefUseEdgeTupleTy> &);
            void updatePointsToSet(const ProgramLocationTy*, const PointerTy*, 
                std::set<const PointerTy*>, SetVector<DefUseEdgeTupleTy>&);
            bool updatePointsToSetAtProgramLocation(const ProgramLocationTy*, const PointerTy*, std::set<const PointerTy*>&);

            std::set<const User*> includeBitCastUsers(const BitCastInst *BC);

            
        public:
            using Result = FlowSensitivePointerAnalysisResult;
            FlowSensitivePointerAnalysisResult run(Module&, ModuleAnalysisManager&);
            FlowSensitivePointerAnalysisResult getResult() {return AnalysisResult;}
    };


    // Since we need to create labels before creating def-use edge, we need to associate an instruction to a series of labels.
    // This class represents a single label. As a label, it records:
    //      1. whether this is a def or use or def-use.
    //      2. the memoryobject being defed or used. 
    struct Label{

        const FlowSensitivePointerAnalysis::PointerTy *Ptr;
        enum class LabelType{
            None = 0, Use, Def, DefUse
        };
        LabelType Type;

        // Label() = default;
        Label(const FlowSensitivePointerAnalysis::PointerTy *Ptr, Label::LabelType Type) : Ptr(Ptr), Type(Type) {}
    };

    raw_ostream& operator<<(raw_ostream&, const Label&);
    bool operator<(const Label&, const Label&);

} //namespace llvm



#endif //LLVM_TRANSFORM_FLOW_SENSITIVE_POINTER_ANALYSIS_H
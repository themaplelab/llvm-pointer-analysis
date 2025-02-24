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

#include "llvm/Transforms/Utils/SteengaardAnalysis.h"


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

        // using PointerTy = Value;
        using ProgramLocationTy = Instruction;
        // using WorkListTy = std::map<size_t, std::set<size_t>>;
        using PointsToSetTy = std::map<const ProgramLocationTy*, std::map<size_t, std::set<size_t>>>;


        // std::map<const Function*, WorkListTy> Worklist;
        PointsToSetTy PointsToSet;
        // std::map<const Function*, SetVector<const Value*>> Func2AllocatedPointersAndParameterAliases;



        public:
            // FlowSensitivePointerAnalysisResult() = default;
            FlowSensitivePointerAnalysisResult(const PointsToSetTy &Pts) : PointsToSet(Pts) {}

            // std::map<const Function*, WorkListTy> getWorkList() {return Worklist;}
            // void setWorkList(std::map<const Function*, WorkListTy> WL) {Worklist = WL; return;}
            PointsToSetTy getPointsToSet(){
                return PointsToSet;
            }
            void setPointsToSet(PointsToSetTy PTS){
                PointsToSet = PTS;
            }

            // std::map<const Function*, SetVector<const Value*>> getFunc2Pointers() {return Func2AllocatedPointersAndParameterAliases;}
            // void setFunc2Pointers(std::map<const Function*, SetVector<const Value*>> F2P){
            //     Func2AllocatedPointersAndParameterAliases = F2P;
            //     return;
            // }
    };

    struct Label;

    class FlowSensitivePointerAnalysis : public AnalysisInfoMixin<FlowSensitivePointerAnalysis>{
        friend AnalysisInfoMixin<FlowSensitivePointerAnalysis>;
        friend Label;

        using PointerTy = Value;
        using ProgramLocationTy = Instruction;
        using PointsToSetTy = std::map<const ProgramLocationTy*, std::map<size_t, std::set<size_t>>>;
        using WorkListTy = std::map<size_t, std::set<size_t>>;
        using DefUseEdgeTupleTy = std::tuple<const ProgramLocationTy*, const ProgramLocationTy*, size_t>;
        using DefUseGraphTy = std::map<const ProgramLocationTy*, std::map<size_t, std::set<const ProgramLocationTy*>>>;

        // Map each pointer to the program location that requires its alias information.
        PointsToSetTy AliasMap;
        std::map<const PointerTy*, std::set<const User*>> AliasUser;
        std::map<const Function*, std::set<const Function*>> Caller2Callee;
        DefUseGraphTy DefUseGraph;
        std::map<const Function*, SetVector<const PointerTy*>> Func2AllocatedPointersAndParameterAliases;
        std::map<const Function*, std::set<const ProgramLocationTy*>> Func2CallerLocation;
        std::map<const Function*, WorkListTy> Func2WorkList; 
        std::map<const Function*, std::set<const BasicBlock*>> Func2TerminateBBs;
        std::map<const Function*, std::set<const ProgramLocationTy*>> Func2Returns;

        std::map<const Function*, PointsToSetTy::mapped_type> FuncParas2PointsToSet;
        WorkListTy GlobalWorkList;
        std::map<const ProgramLocationTy*, std::set<Label>> LabelMap; 
        PointsToSetTy PointsToSetOut;
        PointsToSetTy PointsToSetIn;
        std::map<size_t, std::set<const ProgramLocationTy*>> UseList;
        std::map<const Function*, std::reference_wrapper<DominatorTreeAnalysis::Result>> Func2DomTree;
        std::map<const Function*, std::reference_wrapper<DominanceFrontierAnalysis::Result>> Func2DomFrontier;
        std::map<size_t, std::map<const Function*, std::set<const ProgramLocationTy*>>> DefLocations;
        std::map<const CallBase*, std::map<size_t, std::set<size_t>>> CallSite2ArgIdx;

        SteengaardAnalysisResult SteengaardResult;

        static AnalysisKey Key;
        static bool isRequired() { return true; }

        private:
            void addDefUseEdge(const ProgramLocationTy*, const ProgramLocationTy*, size_t);
            void addDefLabel(size_t Ptr, const ProgramLocationTy *Loc, const Function *Func);
            void addUseLabel(size_t Ptr, const ProgramLocationTy *Loc);
            std::pair<std::map<const Instruction*, std::set<const Instruction*>>, DomGraph> 
                buildDominatorGraph(const Function *Func, size_t PtrId);
            void buildDefUseGraph(std::set<const ProgramLocationTy*>, size_t, 
                std::map<const Instruction*, std::set<const Instruction*>>, DomGraph);
            size_t computePointerLevel(const PointerTy*, bool isTopLevel);
            void dumpAliasMap();
            void dumpLabelMap();
            void dumpPointsToSet();
            std::vector<const ProgramLocationTy*> getAffectUseLocations(const ProgramLocationTy*, size_t);
            std::set<size_t> getAlias(const ProgramLocationTy*, const LoadInst*);
            std::set<size_t> getRealPointsToSet(const ProgramLocationTy*, const PointerTy*);
            std::set<const ProgramLocationTy*> getUseLocations(size_t);
            void globalInitialize(Module&);
            bool hasDef(const ProgramLocationTy*, size_t);
            void initialize(const Function*);
            SetVector<DefUseEdgeTupleTy> initializePropagateList(std::set<size_t>, size_t, const Function *);
            bool insertPointsToSetAtProgramLocation(const ProgramLocationTy *, size_t, std::set<size_t>&);
            void markLabelsForPtr(const PointerTy*, bool isTopLevel);
            void printPointsToSetAtProgramLocation(const ProgramLocationTy*);
            void processGlobalVariables(size_t);
            void propagate(SetVector<DefUseEdgeTupleTy>, const Function*);
            void propagatePointsToInformation(const ProgramLocationTy*, const ProgramLocationTy*, size_t);
            std::vector<size_t> ptsPointsTo(const ProgramLocationTy*, const PointerTy*);
            void updateAliasInformation(const ProgramLocationTy *, size_t, size_t);
            void updateAliasUsers(const ProgramLocationTy*, size_t, size_t, SetVector<DefUseEdgeTupleTy>&);
            void updateArgPointsToSetOfFunc(const Function*, std::set<size_t>, size_t, SetVector<DefUseEdgeTupleTy> &);
            void updatePointsToSet(const ProgramLocationTy*, size_t, 
                std::set<size_t>, SetVector<DefUseEdgeTupleTy>&);
            bool updatePointsToSetAtProgramLocation(const ProgramLocationTy*, size_t, std::set<size_t>&);
            std::set<size_t> getPointsToSet(size_t Ptr, const ProgramLocationTy *Store);

            bool isAlias(size_t LoadId, size_t PtrId, const PointerTy *Loc);

            double computeAvgPtsSize();
            void dumpWorkList();
            void dumpDefUseGraph();

            const std::set<size_t>& getPointersInWorkList(size_t PointerLevel, const Function *Func);

            
        public:
            using Result = FlowSensitivePointerAnalysisResult;
            FlowSensitivePointerAnalysisResult run(Module&, ModuleAnalysisManager&);
    };


    // Since we need to create labels before creating def-use edge, we need to associate an instruction to a series of labels.
    // This class represents a single label. As a label, it records:
    //      1. whether this is a def or use or def-use.
    //      2. the memoryobject being defed or used. 
    struct Label{

        size_t Ptr;
        enum class LabelType{
            None = 0, Use, Def, DefUse
        };
        LabelType Type;

        // Label() = default;
        Label(size_t Ptr, Label::LabelType Type) : Ptr(Ptr), Type(Type) {}
        bool operator=(const Label &l){
            return this->Ptr == l.Ptr && this->Type==l.Type;
        }
    };

    raw_ostream& operator<<(raw_ostream&, const Label&);
    bool operator<(const Label&, const Label&);

} //namespace llvm



#endif //LLVM_TRANSFORM_FLOW_SENSITIVE_POINTER_ANALYSIS_H
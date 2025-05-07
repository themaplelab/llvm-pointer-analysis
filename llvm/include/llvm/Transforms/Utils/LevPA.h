#ifndef LLVM_TRANSFORM_UTIL_LEVPA_H
#define LLVM_TRANSFORM_UTIL_LEVPA_H


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
#include "llvm/Transforms/Utils/FlowSensitivePointerAnalysis.h"


namespace llvm{

    class LevPaResult{

    };



    class LevPA : public AnalysisInfoMixin<LevPA>{
        using PointerTy = Value;
        using WorkListTy = std::map<size_t, std::set<size_t>>;
        using ProgramLocationTy = Instruction;
        using PointsToSetTy = std::map<const ProgramLocationTy*, std::map<size_t, std::set<size_t>>>;
        using DefUseGraphTy = std::map<const ProgramLocationTy*, std::map<size_t, std::set<const ProgramLocationTy*>>>;



        SteengaardAnalysisResult SteengaardResult;
        PointsToSetTy PointsToSetOut;
        PointsToSetTy PointsToSetIn;
        PointsToSetTy AliasMap;
        DefUseGraphTy DefUseGraph;
        DefUseGraphTy UseDefGraph;
        std::map<size_t, std::set<size_t>> LevPaPts;


        std::map<size_t, std::set<const ProgramLocationTy*>> UseList;
        std::map<const Function*, std::reference_wrapper<DominatorTreeAnalysis::Result>> Func2DomTree;
        std::map<const Function*, std::reference_wrapper<DominanceFrontierAnalysis::Result>> Func2DomFrontier;
        std::map<size_t, std::map<const Function*, std::set<const ProgramLocationTy*>>> DefLocations;
        std::map<const Function*, std::set<const ProgramLocationTy*>> Func2CallerLocation;
        std::map<const CallBase*, std::map<size_t, std::set<size_t>>> CallSite2ArgIdx;
        std::map<const Function*, std::set<const ProgramLocationTy*>> Func2Returns;
        std::map<const Function*, WorkListTy> Func2WorkList; 
        std::map<const ProgramLocationTy*, std::set<Label>> LabelMap; 
        std::map<const Instruction*, std::map<size_t,size_t>> AdditionalPointerIdMapIn;
        std::map<const Instruction*, std::map<size_t,size_t>> AdditionalPointerIdMapOut;
        std::map<size_t, std::set<std::tuple<size_t, size_t, bool>>> PointerLevelToConstraints;
        std::map<size_t, std::set<size_t>> CopyGraph;

        LevPaResult AnalysisResult;


        public:

            static AnalysisKey Key;
            static bool isRequired() { return true; }

            using Result = LevPaResult;
            Result run(Module&, ModuleAnalysisManager&);


        private:
            void globalInitialize(Module&);
            void initialize(const Function*);
            const Instruction* getFirstInst(const Function*);
            void addDefLabel(size_t, const ProgramLocationTy*);
            void addUseLabel(size_t, const ProgramLocationTy*);
            size_t computePointerLevel(size_t);
            const std::set<size_t>& getPointersInWorkList(size_t, const Function*);
            void markLabelsForPtr(const PointerTy*, bool);
            void markLabelsAtUser(const PointerTy*, size_t, const User*);
            std::set<size_t> getPointsToSet(size_t, const ProgramLocationTy*);
            std::pair<std::map<const Instruction*, std::set<const Instruction*>>, DomGraph> buildDominatorGraph(const Function*, size_t);
            std::set<const ProgramLocationTy*> getUseLocations(size_t);
            void buildDefUseGraph(std::set<const ProgramLocationTy*>, size_t, std::map<const Instruction*, std::set<const Instruction*>>, DomGraph);
            void addDefUseEdge(const ProgramLocationTy*, const ProgramLocationTy*, size_t);

            std::set<size_t> getLevPaPts(size_t);
            std::set<size_t> getLastVersion(size_t Pointer, const Instruction *Loc);
            size_t getCurrentVersion(size_t Pointer, const Instruction *Loc);
            size_t getCurrentVersionOut(size_t Pointer, const Instruction *Loc);
            std::set<const Instruction*> getDefinitionLocs(size_t Pointer, const Instruction *Loc);
            void createNewVersionOfPointer(size_t PointerId, const Instruction *Loc);
            void createCopyRule(size_t Lhs, size_t Rhs, size_t pl);
            void createAllocaRule(size_t TopLvlId, size_t AddrTakenId, size_t pl);
            void createStrongUpdateRule(size_t CurrentVersion, size_t Pointer, size_t pl);
            void createWeakUpdateRule(size_t CurrentVersion, std::set<size_t> LastVersions, size_t ValueOpId, size_t pl);
            void solveConstraints(size_t CurrentPointerLevel);
            void markLabelsforNextPointerLevel(size_t CurrentPointerLevel);
            std::set<size_t> getPointsToSetHelper(size_t PtrId, const ProgramLocationTy *Loc, std::set<size_t> &Visited);






            size_t index;








    };

}







#endif
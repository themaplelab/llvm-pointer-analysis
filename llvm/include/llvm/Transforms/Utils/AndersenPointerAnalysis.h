#ifndef LLVM_TRANSFORM_ANDERSEN_POINTER_ANALYSIS_H
#define LLVM_TRANSFORM_ANDERSEN_POINTER_ANALYSIS_H

#include "llvm/IR/Instructions.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/PassManager.h"

#include <map>
#include <set>



namespace llvm{

    class ConstraintGraph{
        public:
            ConstraintGraph() = default;
            ~ConstraintGraph(){};

            void addNode(const Value *Node){
                Nodes.insert(Node);
            }

            std::set<const Value *> getNodes(){
                return Nodes;
            }

            bool addEdge(const Value *From, const Value *To){
                addNode(From);
                addNode(To);
                auto Pair = Edges[From].insert(To);
                return Pair.second;
            }

            std::set<const Value *> getEdges(const Value *Node){
                return Edges[Node];
            }


        private:
            std::set<const Value *> Nodes;
            std::map<const Value *, std::set<const Value *>> Edges;
    };

    class AndersenPointerAnalysisResult{

        std::map<const Value*, std::set<const Value*>> PointsToSet;


        public:
            std::map<const Value*, std::set<const Value*>> getPointsToSet(){
                return PointsToSet;
            }
            void setPointsToSet(std::map<const Value*, std::set<const Value*>> PTS){
                PointsToSet = PTS;
            }

    };

    class AndersenPointerAnalysis : public AnalysisInfoMixin<AndersenPointerAnalysis>{

        friend AnalysisInfoMixin<AndersenPointerAnalysis>;

        static AnalysisKey Key;
        static bool isRequired() { return true; }

        std::map<const Value*, std::set<const Value*>> PointsToSet;
        std::map<const Function*, std::set<const Value*>> WorkList;
        ConstraintGraph CG;
        std::map<const Function*, std::set<const Instruction*>> Func2CallSites;
        AndersenPointerAnalysisResult AnalysisResult;


        public:
            using Result = AndersenPointerAnalysisResult;
            AndersenPointerAnalysisResult run(Module&, ModuleAnalysisManager&);
            AndersenPointerAnalysisResult getResult() {return AnalysisResult;}



    };

}   //namespace llvm


#endif
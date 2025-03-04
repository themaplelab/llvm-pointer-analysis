#ifndef LLVM_TRANSFORM_UTILS_STEENGAARD_ANALYSIS_H
#define LLVM_TRANSFORM_UTILS_STEENGAARD_ANALYSIS_H

#include "llvm/IR/PassManager.h"
#include "llvm/IR/Value.h"

#include <map>
#include <set>
#include <stack>




namespace llvm{

    

    class UnionFind{
        private:
            std::map<size_t, size_t> Parent;
        public:
            size_t find(size_t Ptr){
                if(Parent.find(Ptr) == Parent.end()){
                    Parent.try_emplace(Ptr, Ptr);
                }

                if(Parent.at(Ptr) != Ptr){
                    Parent.at(Ptr) = find(Parent.at(Ptr));
                }

                return Parent.at(Ptr);
            }

            void merge(size_t Ptr1, size_t Ptr2){
                auto Parent1 = find(Ptr1);
                auto Parent2 = find(Ptr2);

                if(Parent1 != Parent2){
                    Parent.at(Parent1) = Parent2;
                }
            }

            std::map<size_t, size_t>& getParent(){
                return Parent;
            }
    };

    class SteengaardAnalysisResult{
        std::map<size_t, std::set<size_t>> Pts;
        std::map<size_t, size_t> PointerLevel;
        std::map<std::pair<const Value*, bool>, size_t> PointerID;
        std::map<size_t, std::pair<const Value*, bool>> ID2Ptr;
        size_t MaxPl;
        UnionFind Uf;
        std::map<size_t, size_t> PtgNodeToSccGroupMap;


        public:
            SteengaardAnalysisResult() = default;
            SteengaardAnalysisResult(const std::map<size_t, std::set<size_t>> &Pts, const std::map<size_t, size_t> &PointerLevel,
                const std::map<std::pair<const Value*, bool>, size_t> &PointerID, const std::map<size_t, std::pair<const Value*, bool>> &ID2Ptr, size_t MaxPl,
                UnionFind Uf, const std::map<size_t, size_t> &PtgNodeToSccGroupMap) : Pts(Pts),
                PointerLevel(PointerLevel), PointerID(PointerID), ID2Ptr(ID2Ptr), MaxPl(MaxPl), Uf(Uf), PtgNodeToSccGroupMap(PtgNodeToSccGroupMap) {}

            size_t getMaxPl(){
                return MaxPl;
            }

            size_t getPointerLevel(size_t Pointer){
                return PointerLevel.at(PtgNodeToSccGroupMap[Uf.find(Pointer)]);
            }


            size_t getID(const Value *Ptr, bool isTopLevel){
                if(PointerID.find({Ptr, isTopLevel}) == PointerID.end()){
                    if(!Ptr){
                        outs() << "nullptr\n";
                    }
                    else{
                        outs() << *Ptr << "\n";
                    }
                    
                    llvm_unreachable("Cannot get id.");
                }
            
                return PointerID.at({Ptr, isTopLevel});
            }

            std::pair<const Value*, bool> getPtr(size_t Id){
                assert(ID2Ptr.count(Id) && "Cannot retrieve id.");
                return ID2Ptr.at(Id);
            }
    };

    class SteengaardAnalysis : public AnalysisInfoMixin<SteengaardAnalysis>{

        
        std::map<std::pair<const Value*, bool>, size_t> pointerID;
        std::map<size_t, std::pair<const Value*, bool>> ID2Ptr;

        std::map<size_t, size_t> AllocatedTopLevelPointsToMap;

        std::map<size_t, std::set<size_t>> PointsToMap;
        std::map<size_t, std::set<size_t>> DagPointsToMap;
        std::map<size_t, size_t> PointerLevel;
        std::set<size_t> Visited;

        std::map<size_t, std::set<size_t>> SCC2Node;
        std::stack<size_t> Stack;
        std::map<size_t, size_t> OnStack;
        
        // Map node in original points-to graph to the node index of DAG.
        std::map<size_t, size_t> PtgNodeToDagNodeMap;
        // Map node to the SCC id it belongs to.
        std::map<size_t, size_t> PtgNodeToSccGroupMap;

        size_t index = 0;


        static size_t id;
        UnionFind Uf;


        public:
            
            static AnalysisKey Key;
            using Result = SteengaardAnalysisResult;

            size_t getID(const Value *Ptr, bool isTopLevel);
            size_t getPointerLevel(size_t Pointer);
            SteengaardAnalysisResult run(Module &M, ModuleAnalysisManager &MAM);
            void printStats();
            

        private:

            void createID(const Value *Ptr, bool isTopLevel = true);
            size_t getPointerLevelForSCCGraph(size_t SCCNode);
            void computePtsAndAlias();
            size_t computeMaxPointerLevel();
            void SCCtoDAG();
            void findSCC(size_t node);
            void verifyResult(Module &M);
    };



}



#endif
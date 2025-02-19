#ifndef LLVM_TRANSFORM_UTILS_STEENGAARD_ANALYSIS_H
#define LLVM_TRANSFORM_UTILS_STEENGAARD_ANALYSIS_H

#include "llvm/IR/PassManager.h"
#include "llvm/IR/Value.h"

#include <map>
#include <set>



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
        std::map<size_t, std::set<size_t>> Alias;
        std::map<size_t, size_t> PointerLevel;
        std::map<std::pair<const Value*, bool>, size_t> pointerID;
        size_t maxPl;


        public:
            void setPts(const std::map<size_t, std::set<size_t>> &Pts){
                this->Pts = Pts;
            }

            void setAlias(const std::map<size_t, std::set<size_t>> &Alias){
                this->Alias = Alias;
            }

            void setPointerLevels(std::map<size_t, size_t> &PointerLevel){
                this->PointerLevel = PointerLevel;
            }

            void setPointerID(std::map<std::pair<const Value*, bool>, size_t> &PointerID){
                this->pointerID = PointerID;
            }

            void setMaxPl(size_t pl){
                this->maxPl = pl;
            }

            size_t getMaxPl(){
                return maxPl;
            }

            const std::map<size_t, size_t>& getPointerLevels(){
                return PointerLevel;
            }

            const std::map<std::pair<const Value*, bool>, size_t> & getPointerIDs(){
                return pointerID;
            }

            size_t getID(const Value *Ptr, bool isTopLevel){
                if(pointerID.find({Ptr, isTopLevel}) == pointerID.end()){
                    errs() << "Cannot find id for pointer " << *Ptr << " " << isTopLevel << "\n";
                    std::terminate();
                }
            
                return pointerID.at({Ptr, isTopLevel});
            }
    };

    class SteengaardAnalysis : public AnalysisInfoMixin<SteengaardAnalysis>{

        
        std::map<std::pair<const Value*, bool>, size_t> pointerID;
        std::map<size_t, std::pair<const Value*, bool>> ID2Ptr;
        std::map<size_t, size_t> Pts;

        std::map<size_t, std::set<size_t>> truePts;
        std::map<size_t, std::set<size_t>> trueAlias;
        std::map<size_t, size_t> PointerLevel;

        static size_t id;
        UnionFind Uf;


        public:
            
            static AnalysisKey Key;

            size_t getID(const Value *Ptr, bool isTopLevel);

            using Result = SteengaardAnalysisResult;
            SteengaardAnalysisResult run(Module &M, ModuleAnalysisManager &MAM);
            void printStats();
            

        private:

            void createID(const Value *Ptr, bool isTopLevel = true);
            size_t getPointerLevel(size_t Pointer);
            void computePtsAndAlias();
            size_t computePointerLevel();
    };



}



#endif
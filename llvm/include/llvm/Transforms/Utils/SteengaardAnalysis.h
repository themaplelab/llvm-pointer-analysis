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
        std::map<std::pair<const Value*, bool>, size_t> PointerID;
        std::map<size_t, std::pair<const Value*, bool>> ID2Ptr;
        size_t MaxPl;


        public:
            SteengaardAnalysisResult() = default;
            SteengaardAnalysisResult(const std::map<size_t, std::set<size_t>> &Pts, const std::map<size_t, std::set<size_t>> &Alias, const std::map<size_t, size_t> &PointerLevel,
                const std::map<std::pair<const Value*, bool>, size_t> &PointerID, const std::map<size_t, std::pair<const Value*, bool>> &ID2Ptr, size_t MaxPl) : Pts(Pts), Alias(Alias),
                PointerLevel(PointerLevel), PointerID(PointerID), ID2Ptr(ID2Ptr), MaxPl(MaxPl) {}

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
                this->PointerID = PointerID;
            }

            void setId2Ptr(const std::map<size_t, std::pair<const Value*, bool>> &ID2Ptr){
                this->ID2Ptr = ID2Ptr;
            }

            void setMaxPl(size_t pl){
                this->MaxPl = pl;
            }

            size_t getMaxPl(){
                return MaxPl;
            }

            const std::map<size_t, size_t>& getPointerLevels(){
                return PointerLevel;
            }

            const std::map<std::pair<const Value*, bool>, size_t> & getPointerIDs(){
                return PointerID;
            }

            size_t getID(const Value *Ptr, bool isTopLevel){
                if(PointerID.find({Ptr, isTopLevel}) == PointerID.end()){
                    errs() << "Cannot find id for pointer " << *Ptr << " " << isTopLevel << "\n";
                    std::terminate();
                }
            
                return PointerID.at({Ptr, isTopLevel});
            }

            std::pair<const Value*, bool> getPtr(size_t Id){
                // errs() << Id << "\n";
                assert(ID2Ptr.count(Id) && "Cannot retrieve id.");
                return ID2Ptr.at(Id);
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
            size_t computeMaxPointerLevel();
    };



}



#endif
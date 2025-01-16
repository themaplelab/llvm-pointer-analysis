#ifndef LLVM_TRANSFORMS_UTILS_GENERATE_DATALOG_FACTS_H
#define LLVM_TRANSFORMS_UTILS_GENERATE_DATALOG_FACTS_H



#include "llvm/IR/PassManager.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/IntrinsicInst.h"

#include <vector>
#include <map>
#include <tuple>

namespace llvm{

    class MyMemoryObject{
        static int cnt;
        public:
            MyMemoryObject() : id(-1), inst(nullptr) {}
            MyMemoryObject(const Value *inst) : id(cnt++), inst(inst) {}
            int getId() const {return id;}
            const Value* getInst() const {return inst;}
        private:
            int id;
            const Value *inst;
    };


    class GenerateDataLogFacts : public PassInfoMixin<GenerateDataLogFacts>{

        public:
            PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM);

        private:
            std::vector<MyMemoryObject> memoryObjects;
            std::map<const Value*, MyMemoryObject> var2mo;
            std::vector<std::tuple<const Instruction*, MyMemoryObject, MyMemoryObject, const Function*>> Allocs;
            std::map<const Instruction*, int> inst2id;
            std::map<const Function*, int> func2id;
            std::vector<std::tuple<const Instruction*, MyMemoryObject, MyMemoryObject>> Loads;
            std::vector<std::tuple<const Instruction*, MyMemoryObject, MyMemoryObject>> Stores;
            std::vector<std::tuple<const Instruction*, MyMemoryObject, MyMemoryObject>> Moves;
            std::vector<std::tuple<const Instruction*, const Function*>> CallGraphs;
            std::vector<std::tuple<const Instruction*, MyMemoryObject, MyMemoryObject, MyMemoryObject>> Phis;
            std::vector<std::tuple<const Function*, size_t, MyMemoryObject>> FormalArgs;
            std::vector<std::tuple<const Function*, size_t, MyMemoryObject>> ActualArgs;
            std::vector<std::tuple<const Function*, MyMemoryObject>> FormalReturns;
            std::vector<std::tuple<const Function*, MyMemoryObject>> ActualReturns;







            MyMemoryObject getMemoryObjectFromValue(const Value *);
            std::vector<const Instruction*> getNextInstructions(const Instruction*);
            void dumpAllocs();
            void dumpLoads();
            void dumpStores();
            void dumpMoves();
            void dumpCallGraphs();
            void dumpPhis();
            void dumpFormalArgs();
            void dumpActualArgs();








    };

}








#endif
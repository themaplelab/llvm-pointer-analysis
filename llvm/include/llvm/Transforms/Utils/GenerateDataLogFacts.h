#ifndef LLVM_TRANSFORMS_UTILS_GENERATE_DATALOG_FACTS_H
#define LLVM_TRANSFORMS_UTILS_GENERATE_DATALOG_FACTS_H



#include "llvm/IR/PassManager.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/Instructions.h"

#include <vector>
#include <map>

namespace llvm{

    class MemoryObject{
        static int cnt;
        public:
            MemoryObject(const Instruction *Inst) : id(cnt++), inst(inst) {}
            int getId() const {return id;}
            const Instruction* getInst() const {return inst;}
        private:
            int id;
            const Instruction *inst;
    };

    int MemoryObject::cnt = 0;


    class GenerateDataLogFacts : public PassInfoMixin<GenerateDataLogFacts>{

        public:
            PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM);

        private:
            std::map<int, MemoryObject> ID2memoryObject;
            std::vector<std::pair<int,int>> Allocs;
            std::map<const Instruction*, int> inst2id;
            std::map<const Function*, int> func2id;

    };

}








#endif
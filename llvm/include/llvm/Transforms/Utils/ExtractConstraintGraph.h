#ifndef LLVM_TRANSFORMS_UTILS_EXTRACTCONSTRAINTGRAPH_H
#define LLVM_TRANSFORMS_UTILS_EXTRACTCONSTRAINTGRAPH_H

#include "llvm/IR/PassManager.h"
#include "llvm/Support/raw_ostream.h"

#include <memory>
#include <vector>
#include <cstdint>
#include <cstring>
#include <iostream>

namespace llvm {

template< typename T >
struct SharedPtrArrayDeleter{
    void operator ()( T const * p)
    { 
        delete[] p; 
    }
};


class SparseBitVector {
    // friend raw_ostream& operator<<(raw_ostream &os, const SparseBitVector &sbv);


    private:
        std::unique_ptr<int[]> Memory;


        size_t GroupID;
        size_t Base;
        size_t Bits;
        SparseBitVector *Next;



    public:
        SparseBitVector(size_t GID, size_t Base, size_t Bits, SparseBitVector *Next = nullptr);

        size_t getGroupID(){
            return Memory[0];
        }

        size_t getBase(){
            return Memory[1];
        }

        size_t getBits(){
            for(int i = 2; i < 30; ++i){
                if(Memory[i] != 0){
                    int base = (29-i) * 8;
                    int offset = 0;
                    while(offset < 32 && (Memory[i] >> offset) != 1){
                        ++offset;
                    }


                    return base + offset+1;
                }
            }

            assert(false && "There must be 1 bit of 1 in Bits.");
        }

        SparseBitVector* getNext(){
            SparseBitVector *ptr;
            std::memcpy(&ptr, &Memory[30], sizeof(ptr));
            return ptr;
        }





};



class ExtractConstraintGraphPass : public PassInfoMixin<ExtractConstraintGraphPass> {
public:
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM);
};




} // namespace llvm








#endif
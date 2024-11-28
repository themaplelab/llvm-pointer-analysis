#include "llvm/Transforms/Utils/ExtractConstraintGraph.h"


using namespace llvm;

static int global = 0;

SparseBitVector::SparseBitVector(size_t GID, size_t Base, size_t Bits, SparseBitVector *Next) : 
        GroupID(GID), Base(Base), Bits(Bits), Next(Next){

        assert(GroupID < 2<<32 && "Invalid group id - exceed 2**32.");
        assert(Base < 2<<32 && "Invalid base - exceed 2**32.");
        assert(Bits < 112 && "Invalid bits - valid range is 0 - 111.");

        Memory = std::unique_ptr<int[]>(new int[32]);

        // set group id, which is in Memory[0]
        Memory[0] = GroupID;
        Memory[1] = Base;
        int byteIdx = (Bits-1) / 32;
        int andCand = (1 << ((Bits-1) % 32));

        for(int i = 0; i < 28; ++i){
            if(byteIdx != i){
                Memory[29-i] = 0;
            }
            else{
                Memory[29-i] = andCand;
            }
        }

        // int *add = &global;
        // auto address = reinterpret_cast<int*>(&add);
        // Memory[30] = address[0];
        // Memory[31] = address[1];
        // assert(sizeof(address) == 8 && "Assume 8 bytes pointer.");

        // outs() << (int*)address << "\n";


        
        auto address = reinterpret_cast<int*>(&global);
        std::memcpy(&Memory[30], &address, sizeof(address));
        

        // outs() << (int*)Memory[30] << " " << (int*)Memory[31] << "\n";
        int *ptr;
        std::memcpy(&ptr, &Memory[30], sizeof(ptr));
        // outs() << ptr << "\n";

    
        
}




PreservedAnalyses ExtractConstraintGraphPass::run(Module &M, ModuleAnalysisManager &MAM){

    // Test SBV

    auto sbv0 = SparseBitVector(0, 0, 1, nullptr);
    auto sbv1 = SparseBitVector(0, 0, 2, (SparseBitVector*)&global);


    assert(sbv0.getGroupID() == 0 && "getGroupID error.");
    assert(sbv0.getBase() == 0 && "getBase error.");
    assert(sbv0.getBits() == 1 && "getBits error.");
    outs() << sbv1.getNext() << " " << &global << "\n";
    assert((int*)sbv1.getNext() == &global && "getNext error");

    
    return PreservedAnalyses::all();

    
}

// raw_ostream& operator<<(raw_ostream &os, const SparseBitVector &sbv){
//     for(int i = 0; i < 32; ++i){
//         for(int j = 31; j >= 0; --j){
//             os << (((*sbv.Memory)[i] >> j) & 1);
//         }
//     }
// }
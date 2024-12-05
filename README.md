# LFSPA: Level Flow Sensitive Pointer Analysis

## Requirement:
    cmake
    ninja
    

## Usage: 

### Build LLVM
    git checkout FSPA-14
    mkdir build
    cd build
    cmake -S ../llvm -G Ninja _DLLVM_ENABLE_PROJECTS='clang' -DCMAKE_BUILD_TYPE=debug
    ninja

### Run LFSPA
    cd build/bin
    ./opt -passes="print-pointer-level" -time-passes -track-memory --disable-output < <#inputFile#>

### Compile your test cases into LLVM IR
    cd build/bin
    ./clang -emit-llvm -c -Xclang -disable-o0-optnone <#InputFile#>

Note `-Xclang -disable-o0-optnone` is required if the optimization level is set to 0, otherwise, LLVM may ignore any optimization pass being run on the LLVM IR.

### Dump readable LLVM IR
    cd build/bin
    ./llvm-dis <#InputFile#>.bc
    


# Flow Sensitive Pointer Analysis with LLVM


## Build LLVM and Pointer Analysis

```
git clone git@github.com:themaplelab/llvm-pointer-analysis.git
git checkout FSPA

cd llvm-pointer-analysis
mkdir build
cd build
cmake -S .. -G Ninja -DLLVM_ENABLE_PROJECTS='clang' -DCMAKE_BUILD_TYPE=debug
ninja

```

## Run Pointer Analysis

* Print Pointer Level:
  * ./opt -passes="fspa-pl-print" \<input-file\> --disable-output
* Print Points-to set:
  * ./opt -passes="fspa-pts-print" \<input-file\> --disable-output
* Print Alias pairs:
  * ./opt -passes="fspa-alias-print" \<input-file\> --disable-output


## Test Cases Dir
* llvm-pointer-analysis/llvm/test/Analysis/FlowSensitivePointerAnalysis/
#ifndef LLVM_TRANSFORM_TESTCALLGRAPH_H
#define LLVM_TRANSFORM_TESTCALLGRAPH_H

#include "llvm/Pass.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/ADT/DepthFirstIterator.h"
#include "llvm/ADT/BreadthFirstIterator.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/Analysis/MemoryLocation.h"
#include "llvm/IR/Instruction.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Value.h"
#include <utility>
#include <vector>
#include <new>
#include <map>
#include <set>

/*
    Run interprocedural pointer analysis on LLVM module. The module should contain all related source code linked with
    llvm-link. 

    Potential bug:
        For some LLVM installation, we need -DNDEBUG to enable traversing CallGraph.
*/

struct Label;

class TestCallGraphWrapper : public llvm::ModulePass{

    std::unique_ptr<llvm::CallGraph> cg;
    llvm::DenseMap<const llvm::Function *, bool> visited; 
    // How to represent a points-to set?
    std::map<const llvm::Instruction*, std::map<const llvm::Instruction*, std::set<const llvm::Value*>>> pointsToSet;
    llvm::DenseMap<const llvm::Instruction*, llvm::MemoryLocation> memoryLocationMap;
    std::map<const llvm::Instruction*, std::set<Label>> labelMap; 
    llvm::DenseMap<size_t, llvm::DenseSet<const llvm::Instruction*>> worklist;
    std::map<const llvm::Value*, std::vector<const llvm::Instruction*>> useList;
    std::map<const llvm::Instruction*, std::map<const llvm::Instruction*, std::vector<const llvm::Instruction*>>> defUseGraph;
    std::map<const llvm::Instruction*, std::map<const llvm::Value*, std::set<const llvm::Value *>>> aliasMap;



private:
    const llvm::Function* getFunctionInCallGrpahByName(std::string name);
    void getCallGraphFromModule(llvm::Module &m){
        cg = std::unique_ptr<llvm::CallGraph>(new llvm::CallGraph(m));
    }
    size_t countPointerLevel(const llvm::Instruction *inst);
    void initialize(const llvm::Function * const func);


    // todo: move intra-procedural pointer analysis here.
    // At each call site, do recursive call on the callee.
    void performPointerAnalysisOnFunction(const llvm::Function* const func);
    void propagate(size_t currentPtrLvl, const llvm::Function* func);
    std::vector<const llvm::Instruction*> findDefFromUse(const llvm::Instruction *ptr, const llvm::Instruction *pi);
    bool hasDef(const llvm::Instruction *pinst, const llvm::Instruction *p);
    std::vector<const llvm::Instruction*> findDefFromBB(const llvm::BasicBlock *pbb, const llvm::Instruction *p);
    std::vector<const llvm::Instruction*> getDUEdgesOfPtrAtClause(std::map<const llvm::Instruction*, std::vector<const llvm::Instruction *>> u2p, const llvm::Instruction *ptr);
    void propagatePointsToInformation(const llvm::Instruction *t, const llvm::Instruction *f, const llvm::Instruction *pvar);
    void calculatePointsToInformationForStoreInst(const llvm::Instruction *t, const llvm::Instruction *pvar, const llvm::StoreInst *pt);
    void updateAliasInformation(const llvm::Instruction *t, const llvm::Instruction *pt);
    std::set<const llvm::Value*> getAlias(const llvm::Instruction *t, const llvm::Instruction *p);
    std::vector<const llvm::Value*> ptsPointsTo(const llvm::Instruction *user, const llvm::Instruction *t);





    void dumpWorkList();
    void dumpLabelMap();
    void dumpDefUseGraph() const;
    void dumpPointsToMap();



public:
    static char ID; // Pass identification, replacement for typeid
    TestCallGraphWrapper() : llvm::ModulePass(ID) {}
    bool runOnModule(llvm::Module &m) override;
};


// Since we need to create labels before creating def-use edge, we need to associate an instruction to a series of labels.
// This class represents a single label. As a label, it records:
//      1. whether this is a def or use or def-use.
//      2. the memoryobject being defed or used. 
struct Label{

    const llvm::Value *p;
    enum class LabelType{
        None = 0, Use, Def, DefUse, Alias, AliasDefine
    };
    LabelType type;

    // Label() = default;
    Label(const llvm::Value *p, Label::LabelType t) : p(p), type(t) {}
};

llvm::raw_ostream& operator<<(llvm::raw_ostream &os, const Label &l);
bool operator<(const Label &l1, const Label &l2);



#endif
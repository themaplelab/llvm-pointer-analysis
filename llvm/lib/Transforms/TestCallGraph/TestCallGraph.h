#ifndef LLVM_TRANSFORM_TESTCALLGRAPH_H
#define LLVM_TRANSFORM_TESTCALLGRAPH_H

#include "llvm/Pass.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Debug.h"
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
#include <stack>

/*
    Run interprocedural pointer analysis on LLVM module. The module should contain all related source code linked with
    llvm-link. 

    Potential bug:
        For some LLVM installation, we need -DNDEBUG to enable traversing CallGraph.
*/

struct Label;

class TestCallGraphWrapper : public llvm::ModulePass{

    using PointerTy = llvm::Instruction;
    using ProgramLocationTy = llvm::Instruction;
    using DefUseEdgeTupleTy = std::tuple<const ProgramLocationTy*, const ProgramLocationTy*, const PointerTy*>;

    std::unique_ptr<llvm::CallGraph> cg;
    llvm::DenseMap<const llvm::Function *, bool> visited; 
    // How to represent a points-to set?
    std::map<const llvm::Instruction*, std::map<const llvm::Instruction*, std::pair<std::set<const llvm::Value*>, bool>>> pointsToSet;
    llvm::DenseMap<const llvm::Instruction*, llvm::MemoryLocation> memoryLocationMap;
    std::map<const llvm::Instruction*, std::set<Label>> labelMap; 
    llvm::DenseMap<size_t, llvm::DenseSet<const llvm::Instruction*>> worklist;
    std::map<const llvm::Value*, std::vector<const llvm::Instruction*>> useList;
    std::map<const ProgramLocationTy*, std::map<const ProgramLocationTy*, std::set<const PointerTy*>>> defUseGraph;
    std::map<const llvm::Instruction*, std::map<const llvm::Value*, std::set<const llvm::Value *>>> aliasMap;
    std::stack<const llvm::Function*> left2Analysis;
    // Map each pointer to the program location that requires its alias information.
    std::map<const llvm::Instruction*, std::set<const llvm::User*>> aliasUser;



private:
    const llvm::Function* getFunctionInCallGrpahByName(std::string name);
    void getCallGraphFromModule(llvm::Module &m){
        cg = std::unique_ptr<llvm::CallGraph>(new llvm::CallGraph(m));
    }
    size_t countPointerLevel(const llvm::AllocaInst *allocaInst);
    void initialize(const llvm::Function * const func);


    // todo: move intra-procedural pointer analysis here.
    // At each call site, do recursive call on the callee.
    void performPointerAnalysisOnFunction(const llvm::Function* const func);
    void propagate(size_t currentPtrLvl, const llvm::Function* func);
    std::vector<const llvm::Instruction*> findDefFromUse(const ProgramLocationTy *loc, const PointerTy *ptr);
    bool hasDef(const llvm::Instruction *pinst, const llvm::Instruction *p);
    std::vector<const llvm::Instruction*> findDefFromBB(const llvm::BasicBlock *bb, const PointerTy *p);
    // std::vector<const llvm::Instruction*> getDUEdgesOfPtrAtClause(std::map<const llvm::Instruction*, std::set<const llvm::Instruction *>> u2p, const llvm::Instruction *ptr);
    std::vector<const TestCallGraphWrapper::ProgramLocationTy*> getAffectUseLocations(const ProgramLocationTy *loc, const PointerTy *ptr);
    
    void propagatePointsToInformation(const ProgramLocationTy *t, const ProgramLocationTy *f, const PointerTy *pvar);
    std::set<const llvm::Value*> calculatePointsToInformationForStoreInst(const ProgramLocationTy *t, const llvm::StoreInst *pt);
    void updateAliasInformation(const ProgramLocationTy *t, const llvm::LoadInst *pt);
    std::set<const llvm::Value*> getAlias(const llvm::Instruction *t, const llvm::Instruction *p);
    std::vector<const llvm::Value*> ptsPointsTo(const llvm::Instruction *user, const llvm::Instruction *t);
    bool notVisited(const llvm::Function *f);
    std::vector<const llvm::Function*> collectAllCallees(const llvm::Function*);
    void addDefUseEdge(const ProgramLocationTy *def, const ProgramLocationTy *use, const PointerTy *ptr);
    void updatePointsToSet(const ProgramLocationTy *loc, const PointerTy *ptr, std::set<const llvm::Value *> pointsToSet, std::vector<DefUseEdgeTupleTy> &propagateList);




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

    const llvm::Value *ptr;
    enum class LabelType{
        None = 0, Use, Def, DefUse
    };
    LabelType type;

    // Label() = default;
    Label(const llvm::Value *p, Label::LabelType tp) : ptr(ptr), type(tp) {}
};

llvm::raw_ostream& operator<<(llvm::raw_ostream &os, const Label &l);
bool operator<(const Label &l1, const Label &l2);



#endif
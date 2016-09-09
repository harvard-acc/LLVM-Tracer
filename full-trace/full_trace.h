#include <fstream>
#include <map>
#include <string>

#include "llvm/Pass.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/BasicBlock.h"

class full_traceImpl;

class fullTrace : public BasicBlockPass {
  public:
    fullTrace();
    virtual ~fullTrace();
    static char ID;

    // This BasicBlockPass may be called by other program.
    // provide a way to set up workload, not just environment variable
    std::string my_workload;

    void setWorkload(std::string workload) {
        this->my_workload = workload;
    };

    virtual bool doInitialization(Module &M);
    virtual bool runOnBasicBlock(BasicBlock &BB);

  private:
    full_traceImpl* Impl;
};

/* Reads a labelmap file and inserts it into the dynamic trace.
 *
 * The contents of the labelmap file are embedded into the instrumented binary,
 * so after the optimization pass is finished, the labelmap file is discarded.
 */
class LabelMapHandler : public ModulePass {
  public:
    LabelMapHandler();
    virtual ~LabelMapHandler();
    virtual bool runOnModule(Module &M);
    static char ID;

  private:
    bool readLabelMap();
    void deleteLabelMap();
    std::string labelmap_str;
};

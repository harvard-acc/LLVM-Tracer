#include <fstream>
#include <map>
#include <string>

#include "llvm/Pass.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/BasicBlock.h"

extern char s_phi[];

// Computed properties of an instruction commonly used by this pass's
// handling functions, regardless of what kind of instruction this is.
struct InstEnv {
  public:
    InstEnv() : line_number(-1), instc(0) {}

    // Function name.
    char funcName[256];
    // Basic block ID. See getBBId().
    char bbid[256];
    // Static instruction ID. See getInstId().
    char instid[256];
    // Source line number.
    int line_number;
    // Static instruction count within this basic block.
    int instc;
};

class Tracer : public BasicBlockPass {
  public:
    Tracer();
    virtual ~Tracer() {}
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
    // Instrumentation functions for different types of nodes.
    void handlePhiNodes(BasicBlock *BB, int &instc, Function *func);
    void handleCallInstruction(Instruction *inst, InstEnv *env);
    void handleNonPhiNonCallInstruction(Instruction *inst, InstEnv *env);
    void handleInstructionResult(Instruction *inst, Instruction *next_inst,
                                 InstEnv *env);

    // s - function ID or register ID or label name
    //
    // Insert instrumentation to print one line about this instruction.
    //
    // This function inserts a call to TL_log0 (the first line of output for an
    // instruction), which largely contains information about this
    // instruction's context: basic block, function, static instruction id,
    // source line number, etc.
    void printFirstLine(Instruction *I, int line_number, const char *func_name,
                        char *bbID, char *instID, unsigned opcode);

    // Insert instrumentation to print a line about an instruction's parameters.
    //
    // Parameters may be instruction input/output operands, function call
    // arguments, return values, etc.
    //
    // Based on the value of datatype, this function inserts calls to
    // TL_log_int or TL_log_double.
    void printParamLine(Instruction *I, int param_num, const char *reg_id,
                        const char *bbId, Type::TypeID datatype,
                        unsigned datasize, Value *value, bool is_reg,
                        const char *prev_bbid = s_phi, bool is_phi = false);

    // Should we trace this function or not?
    bool trace_or_not(std::string& func);
    // Does this function appear in our list of tracked functions?
    bool is_tracking_function(std::string& func);
    // Is this function one of the special DMA functions?
    bool is_dma_function(std::string& funcName);

    // Get the bitwidth of this type.
    int getMemSize(Type *T);

    // Construct an ID for the given instruction.
    //
    // Store the value in instid.
    bool getInstId(Instruction *itr, char *bbid, char *instid, int &instc);

    // Construct an ID for the given basic block.
    //
    // Store the value in bbid.
    void getBBId(Value *BB, char *bbid);

    // References to the logging functions.
    Value *TL_log0;
    Value *TL_log_int;
    Value *TL_log_double;

    // The current module.
    Module *curr_module;

    // The current function being instrumented.
    Function *curr_function;

    // Local slot tracker for the current function.
    SlotTracker *st;

    // All functions we are tracking.
    std::set<std::string> tracked_functions;

    // True if WORKLOAD specifies a single function, in which case the tracer
    // will track all functions called by it (the top-level function).
    bool is_toplevel_mode;
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

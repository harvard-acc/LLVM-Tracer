#include <fstream>
#include <map>
#include <string>

#include "llvm/Pass.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/BasicBlock.h"

extern char s_phi[];

// Computed properties of an instruction commonly used by this pass's
// handling functions.
struct InstEnv {
  public:
    InstEnv() : line_number(-1), instc(0) {}

    enum { BUF_SIZE = 256 };

    // Function name.
    char funcName[BUF_SIZE];
    // Basic block ID. See getBBId().
    char bbid[BUF_SIZE];
    // Static instruction ID. See getInstId().
    char instid[BUF_SIZE];
    // Source line number.
    int line_number;
    // Static instruction count within this basic block.
    int instc;
};

struct InstOperandParams {
  public:
    InstOperandParams()
        : param_num(-1), datatype(Type::VoidTyID), datasize(0), is_reg(true) {}

    // Operand index (first, second, etc).
    int param_num;
    // Datatype.
    Type::TypeID datatype;
    // Bitwidth of this operand.
    unsigned datasize;
    // This operand was stored in a register.
    bool is_reg;
    // Value of this operand, if it has one. PHI nodes generally do not have values.
    Value* value;

    char *reg_id;
    char *bbid;
    char *prev_bbid;
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
    void handlePhiNodes(BasicBlock *BB, InstEnv *env);
    void handleCallInstruction(Instruction *inst, InstEnv *env);
    void handleNonPhiNonCallInstruction(Instruction *inst, InstEnv *env);
    void handleInstructionResult(Instruction *inst, Instruction *next_inst,
                                 InstEnv *env);

    // Set line number information in env for this inst if it is found.
    void setLineNumberIfExists(Instruction *I, InstEnv *env);

    // Insert instrumentation to print one line about this instruction.
    //
    // This function inserts a call to TL_log0 (the first line of output for an
    // instruction), which largely contains information about this
    // instruction's context: basic block, function, static instruction id,
    // source line number, etc.
    void printFirstLine(Instruction *insert_point, InstEnv *env, unsigned opcode);

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
                        const char *prev_bbid = s_phi);

    void printParamLine(Instruction *I, InstOperandParams *params);

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
    // If the instruction produces a value in a register, this ID is set to the
    // name of the variable that contains the result or the local slot number
    // of the register, and this function returns true.
    //
    // If the instruction does not produce a value, an artificial ID is
    // constructed by concatenating the given bbid, which both must not be NULL
    // and the current instc.
    //
    // The ID is returned as a string in instid.
    bool getInstId(Instruction *I, char *bbid, char *instid, int *instc);

    // Construct an ID for the given basic block.
    //
    // This ID is either the name of the basic block (e.g. ".lr.ph",
    // "_crit_edge") or the local slot number (which would appear in the IR as
    // "; <label>:N".
    //
    // The ID is stored as a string in bbid.
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

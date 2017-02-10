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
    // opty - opcode or data type
    void print_line(BasicBlock::iterator itr, int line, int line_number,
                    const char *func_or_reg_id, char *bbID, char *instID,
                    int opty, int datasize = 0, Value *value = nullptr,
                    bool is_reg = 0, char *prev_bbid = s_phi);

    void createCallForParameterLine(BasicBlock::iterator itr, int line,
                                    int datasize, int datatype = 64,
                                    bool is_reg = 0,
                                    const char *reg_id = nullptr,
                                    Value *value = nullptr, bool is_phi = 0,
                                    char *prev_bbid = s_phi);

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

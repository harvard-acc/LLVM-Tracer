#include <fstream>
#include <map>
#include <string>

#include "llvm/Pass.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/BasicBlock.h"

extern char s_phi[];

// Get the bitwidth of this type.
int getMemSize(Type *T);

// Computed properties of an instruction commonly used by this pass's
// handling functions.
struct InstEnv {
  public:
    InstEnv() : line_number(-1), instc(0), to_fxpt(false) {}

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
    // Convert all fp operations to fxpt.
    bool to_fxpt;
};

struct InstOperandParams {
  public:
    InstOperandParams()
        : param_num(-1), datatype(Type::VoidTyID), datasize(0), is_reg(true),
          is_intrinsic(false), value(nullptr), operand_name(nullptr),
          bbid(nullptr), prev_bbid(s_phi) {}

    InstOperandParams(const InstOperandParams &other)
        : param_num(other.param_num), datatype(other.datatype),
          datasize(other.datasize), is_reg(other.is_reg),
          is_intrinsic(other.is_intrinsic), value(other.value),
          operand_name(other.operand_name), bbid(other.bbid),
          prev_bbid(other.prev_bbid) {}

    void setDataTypeAndSize(Value* value) {
        datatype = value->getType()->getTypeID();
        datasize = getMemSize(value->getType());
    }

    // Operand index (first, second, etc).
    int param_num;
    // Datatype.
    Type::TypeID datatype;
    // Bitwidth of this operand.
    unsigned datasize;
    // This operand was stored in a register.
    bool is_reg;
    // True if this instruction is a call to an intrinsic function.
    bool is_intrinsic;
    // Value of this operand, if it has one. PHI nodes generally do not have values.
    Value* value;

    char *operand_name;
    char *bbid;
    char *prev_bbid;
};

class Tracer : public FunctionPass {
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
    virtual bool runOnFunction(Function& F);
    virtual bool runOnBasicBlock(BasicBlock &BB);
    virtual void getAnalysisUsage(AnalysisUsage& Info) const;

  private:
    // Instrumentation functions for different types of nodes.
    void handlePhiNodes(BasicBlock *BB, InstEnv *env);
    void handleCallInstruction(Instruction *inst, InstEnv *env);
    void handleNonPhiNonCallInstruction(Instruction *inst, InstEnv *env);
    void handleInstructionResult(Instruction *inst, Instruction *next_inst,
                                 InstEnv *env);

    // Instrument function arguments for print-out upon entry.
    //
    // By printing the arguments from WITHIN the called function, rather than
    // OUTSIDE at the Call instruction, we resolve the problem of potentially
    // not knowing the complete function signature information.  This is
    // because the function may be defined in a different module than the one
    // from which it is being called, and in this case, it's impossible to know
    // what the function argument names are until we run the optimization pass
    // on that module.
    bool runOnFunctionEntry(Function& func);

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
                        bool is_intrinsic, const char *prev_bbid = s_phi);

    void printParamLine(Instruction *I, InstOperandParams *params);

    // Print the first line of a top-level function signature.
    //
    // This has the form "entry,func_name,num_params".
    void printTopLevelEntryFirstLine(Instruction *I, InstEnv *env,
                                     int num_params);

    // Should we trace this function or not?
    bool traceOrNot(const std::string& func);
    // Does this function appear in our list of tracked functions?
    bool isTrackedFunction(const std::string& func);
    // Is this function one of the special DMA functions?
    bool isDmaFunction(std::string& funcName);
    // Is this function an LLVM intrinsic?
    bool isLLVMIntrinsic(const std::string& func);

    // Construct an ID for the given instruction.
    //
    // If the instruction produces a value in a register, this ID is set to the
    // name of the variable that contains the result or the local slot number
    // of the register, and this function returns true.
    //
    // If the instruction does not produce a value, an artificial ID is
    // constructed by concatenating the given bbid, which both must not be NULL
    // and the current instc, and this function returns false.
    //
    // The ID is stored in instid.
    bool getInstId(Instruction *I, char *bbid, char *instid, int *instc);

    // Construct an ID using the given instruction and environment.
    bool getInstId(Instruction *I, InstEnv *env);

    // Get and set the operand name for this instruction.
    bool setOperandNameAndReg(Instruction *I, InstOperandParams *params);

    // Get the variable name for this locally allocated variable.
    //
    // An alloca instruction allocates a certain amount of memory on the stack.
    // This instruction will name the register that stores the pointer
    // with the original source name if it is not locally allocated on the
    // stack. If it is a local variable, it may just store it to a temporary
    // register and name it with a slot number. In this latter case, track down
    // the original variable name using debug information and store it in the
    // slotToVarName map.
    void processAllocaInstruction(BasicBlock::iterator it);

    // Construct a string ID for the given Value object.
    //
    // This ID is either the name of the object or the local slot number.
    // If the object is a BasicBlock, then we append the current loop depth D
    // of this basic block to the end of the ID like name:D.
    //
    // The ID is stored as a string in id_str.
    void makeValueId(Value *value, char *id_str);

    // Convert a floating point opcode to its corresponding integer opcode.
    //
    // When more than one integer opcode is possible, prefer the signed
    // version. If a conversion is not applicable (e.g. branch, and, ret,
    // etc.), return the input opcode.
    unsigned opcodeToFixedPoint(unsigned opcode);

    // Return a pointer to this vector value.
    //
    // The vector data is not guaranteed to have a memory address (it could be
    // just a register). In order to print the value, we need to get a pointer
    // to the first byte and pass that to the tracing function.  To do this, we
    // need to allocate a buffer, store the vector data into that buffer, and
    // return a pointer to the buffer.
    //
    // The buffer is allocated on the stack, so it can and SHOULD be reused;
    // otherwise, for vector-heavy workloads, we will easily run into the stack
    // size limit.
    Value *createVectorArg(Value *vector, IRBuilder<> &IRB);

    // Get a global string constant for str.
    //
    // If such a string has not been allocated a global variable before, then
    // create the argument and return the pointer to the Constant; otherwise,
    // just return the Constant*.
    Constant *createStringArgIfNotExists(const char *str);

    // References to the logging functions.
    Value *TL_log0;
    Value *TL_log_int;
    Value *TL_log_ptr;
    Value *TL_log_double;
    Value *TL_log_vector;
    Value *TL_log_entry;

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

    // Map of stack-allocated vector buffers and their sizes.
    //
    // Whenever we need a buffer, we look up its size to see if we already have
    // one allocated, and return a pointer to that buffer if so.
    std::map<unsigned, AllocaInst*> vector_buffers;

    // Map of strings to newly created global variables storing them.
    std::map<std::string, Constant*> global_strings;

    // Stores names of local variables allocated by alloca.
    //
    // For alloca instructions that allocate local memory, this maps the
    // register name (aka slot number) to the name of the variable itself.
    //
    // Since slot numbers are reused across functions, this has to be cleared
    // when we switch to a new function.
    std::map<unsigned, std::string> slotToVarName;
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

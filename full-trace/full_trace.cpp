#include <vector>
#include <map>
#include <cmath>
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/CommandLine.h"
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <sys/stat.h>
#include "SlotTracker.h"
#include "full_trace.h"

#if (LLVM_VERSION == 34)
  #include "llvm/DebugInfo.h"
#elif (LLVM_VERSION == 35)
  #include "llvm/IR/DebugInfo.h"
#endif

#define RESULT_LINE 19134
#define FORWARD_LINE 24601
#define DMA_FENCE 97
#define DMA_STORE 98
#define DMA_LOAD 99
#define SINE 102
#define COSINE 103

char s_phi[] = "phi";
using namespace llvm;
using namespace std;

cl::opt<string> labelMapFilename("i",
                                 cl::desc("Name of the labelmap file."),
                                 cl::value_desc("filename"),
                                 cl::init("labelmap"));

cl::opt<bool>
    verbose("verbose-tracer",
            cl::desc("Print verbose debugging output for the tracer."),
            cl::init(false), cl::ValueDisallowed);

cl::opt<bool>
    traceAllCallees("trace-all-callees",
                    cl::desc("If specified, all functions called by functions "
                             "specified in the env variable WORKLOAD "
                             "will be traced, even if there are multiple "
                             "functions in WORKLOAD. This means that each "
                             "function can act as a \"top-level\" function."),
                    cl::init(false), cl::ValueDisallowed);

namespace {

  void split(const std::string &s, const char delim, std::set<std::string> &elems) {
      std::istringstream ss(s);
      std::string item;
      while (std::getline(ss, item, delim)) {
          elems.insert(item);
      }
  }

std::vector<std::string> intrinsics = {
  "llvm.memcpy",  // standard C lib
  "llvm.memmove",
  "llvm.memset",
  "llvm.sqrt",
  "llvm.powi",
  "llvm.sin",
  "llvm.cos",
  "llvm.pow",
  "llvm.exp",
  "llvm.exp2",
  "llvm.log",
  "llvm.log10",
  "llvm.log2",
  "llvm.fma",
  "llvm.fabs",
  "llvm.copysign",
  "llvm.floor",
  "llvm.ceil",
  "llvm.trunc",
  "llvm.rint",
  "llvm.nearbyint",
  "llvm.round",
  "llvm.bswap",  // bit manipulation
  "llvm.ctpop",
  "llvm.ctlz",
  "llvm.cttz",
  "llvm.sadd.with.overflow",  // arithmetic with overflow
  "llvm.uadd.with.overflow",
  "llvm.ssub.with.overflow",
  "llvm.usub.with.overflow",
  "llvm.smul.with.overflow",
  "llvm.umul.with.overflow",
  "llvm.fmuladd",  // specialised arithmetic
};

}// end of anonymous namespace

// The vector data is not guaranteed to have a memory address (it could be just
// a register). In order to print the value, we need to get a pointer to the
// first byte and pass that to the tracing function.  To do this, we need to
// allocate an array, store the vector data into that array, and return a
// pointer to the first byte.
static Value *createVectorArg(Value *vector, IRBuilder<> &IRB) {
  Type* vector_type = vector->getType();
  assert(vector_type->isVectorTy());
  Value* alloca_size = ConstantInt::get(IRB.getInt64Ty(), 1);
  AllocaInst *alloca = IRB.CreateAlloca(vector_type, alloca_size);
  StoreInst *store = IRB.CreateAlignedStore(
      vector, alloca, vector_type->getScalarSizeInBits() / 8);
  Value* bitcast = IRB.CreatePointerCast(alloca, IRB.getInt8PtrTy());
  return bitcast;
}

static Constant *createStringArg(const char *string, Module *curr_module) {
    Constant *v_string =
        ConstantDataArray::getString(curr_module->getContext(), string, true);
    ArrayType *ArrayTy_0 = ArrayType::get(
        IntegerType::get(curr_module->getContext(), 8), (strlen(string) + 1));
    GlobalVariable *gvar_array = new GlobalVariable(
        *curr_module, ArrayTy_0, true, GlobalValue::PrivateLinkage, 0, ".str");
    gvar_array->setInitializer(v_string);
    std::vector<Constant *> indices;
    ConstantInt *zero = ConstantInt::get(curr_module->getContext(),
                                         APInt(32, StringRef("0"), 10));
    indices.push_back(zero);
    indices.push_back(zero);
    return ConstantExpr::getGetElementPtr(gvar_array, indices);
}

int getMemSize(Type *T) {
  int size = 0;
  if (T->isPointerTy())
    return 8 * 8;
  else if (T->isFunctionTy())
    size = 0;
  else if (T->isLabelTy())
    size = 0;
  else if (T->isStructTy()) {
    StructType *S = dyn_cast<StructType>(T);
    for (unsigned i = 0; i != S->getNumElements(); i++) {
      Type *t = S->getElementType(i);
      size += getMemSize(t);
    }
  } else if (T->isFloatingPointTy()) {
    switch (T->getTypeID()) {
    case llvm::Type::HalfTyID: ///<  1: 16-bit floating point typ
      size = 16;
      break;
    case llvm::Type::FloatTyID: ///<  2: 32-bit floating point type
      size = 4 * 8;
      break;
    case llvm::Type::DoubleTyID: ///<  3: 64-bit floating point type
      size = 8 * 8;
      break;
    case llvm::Type::X86_FP80TyID: ///<  4: 80-bit floating point type (X87)
      size = 10 * 8;
      break;
    case llvm::Type::FP128TyID:
      ///<  5: 128-bit floating point type (112-bit mantissa)
      size = 16 * 8;
      break;
    case llvm::Type::PPC_FP128TyID:
      ///<  6: 128-bit floating point type (two 64-bits, PowerPC)
      size = 16 * 8;
      break;
    default:
      fprintf(stderr, "!!Unknown floating point type size\n");
      assert(false && "Unknown floating point type size");
    }
  } else if (T->isIntegerTy())
    size = cast<IntegerType>(T)->getBitWidth();
  else if (T->isVectorTy())
    size = cast<VectorType>(T)->getBitWidth();
  else if (T->isArrayTy()) {
    ArrayType *A = dyn_cast<ArrayType>(T);
    size = (int)A->getNumElements() *
           A->getElementType()->getPrimitiveSizeInBits();
  } else {
    fprintf(stderr, "!!Unknown data type: %d\n", T->getTypeID());
    assert(false && "Unknown data type");
  }

  return size;
}

Tracer::Tracer() : FunctionPass(ID) {}

bool Tracer::doInitialization(Module &M) {
  std::string func_string;
  if (this->my_workload.empty()) {
    char* workload = getenv("WORKLOAD");
    if (workload)
        func_string = workload;
  } else {
    func_string = this->my_workload;
  }

  auto &llvm_context = M.getContext();
  auto I1Ty = Type::getInt1Ty(llvm_context);
  auto I64Ty = Type::getInt64Ty(llvm_context);
  auto I8PtrTy = Type::getInt8PtrTy(llvm_context);
  auto VoidTy = Type::getVoidTy(llvm_context);
  auto DoubleTy = Type::getDoubleTy(llvm_context);

  // Add external trace_logger function declaratio
  TL_log0 = M.getOrInsertFunction( "trace_logger_log0", VoidTy,
      I64Ty, I8PtrTy, I8PtrTy, I8PtrTy, I64Ty, I1Ty, I1Ty, nullptr);

  TL_log_int = M.getOrInsertFunction( "trace_logger_log_int", VoidTy,
      I64Ty, I64Ty, I64Ty, I64Ty, I8PtrTy, I64Ty, I8PtrTy, nullptr);

  TL_log_double = M.getOrInsertFunction( "trace_logger_log_double", VoidTy,
      I64Ty, I64Ty, DoubleTy, I64Ty, I8PtrTy, I64Ty, I8PtrTy, nullptr);

  TL_log_vector = M.getOrInsertFunction( "trace_logger_log_vector", VoidTy,
      I64Ty, I64Ty, I8PtrTy, I64Ty, I8PtrTy, I64Ty, I8PtrTy, nullptr);

  if (func_string.empty()) {
    errs() << "\n\nPlease set WORKLOAD as an environment variable!\n\n\n";
    return false;
  }
  std::set<std::string> user_workloads;
  split(func_string, ',', user_workloads);

  // We will instrument in top level mode if there is only one workload
  // function or if explicitly told to do so.
  is_toplevel_mode = (user_workloads.size() == 1) || traceAllCallees;
  if (is_toplevel_mode && verbose)
    std::cout << "LLVM-Tracer is instrumenting this workload in top-level mode.\n";

  st = createSlotTracker(&M);
  st->initialize();
  curr_module = &M;
  curr_function = nullptr;

  DebugInfoFinder Finder;
  Finder.processModule(M);

  #if (LLVM_VERSION == 34)
    auto it = Finder.subprogram_begin();
    auto eit = Finder.subprogram_end();
  #elif (LLVM_VERSION == 35)
    auto it = Finder.subprograms().begin();
    auto eit = Finder.subprograms().end();
  #endif

  for (auto i = it; i != eit; ++i) {
    DISubprogram S(*i);

    auto MangledName = S.getLinkageName().str();
    auto Name = S.getName().str();

    assert(Name.size() || MangledName.size());

    // Checks out whether Name or Mangled Name matches.
    auto MangledIt = user_workloads.find(MangledName);
    bool isMangledMatch = MangledIt != user_workloads.end();

    auto PreMangledIt = user_workloads.find(Name);
    bool isPreMangledMatch = PreMangledIt != user_workloads.end();

    if (isMangledMatch | isPreMangledMatch) {
      if (MangledName.empty()) {
        this->tracked_functions.insert(Name);
      } else {
        this->tracked_functions.insert(MangledName);
      }
    }
  }

  return false;
}

bool Tracer::runOnFunction(Function &F) {
  for (auto bb_it = F.begin(); bb_it != F.end(); ++bb_it) {
    BasicBlock& bb = *bb_it;
    runOnBasicBlock(bb);
  }
  return false;
}

bool Tracer::runOnBasicBlock(BasicBlock &BB) {
  Function *func = BB.getParent();
  std::string funcName = func->getName().str();
  InstEnv env;
  strncpy(env.funcName, funcName.c_str(), InstEnv::BUF_SIZE);
  // Functions suffixed with "_fxp" will have all relevant FP ops printed in the
  // trace as fixed-point ops instead.
  if (funcName.rfind("_fxp", funcName.size() - 4) != std::string::npos)
    env.to_fxpt = true;

  if (curr_function != func) {
    st->purgeFunction();
    st->incorporateFunction(func);
    curr_function = func;
    slotToVarName.clear();
  }

  if (!is_toplevel_mode && !isTrackedFunction(funcName))
    return false;

  if (isDmaFunction(funcName))
    return false;

  if (verbose)
    std::cout << "Tracking function: " << funcName << std::endl;

  // We have to get the first insertion point before we insert any
  // instrumentation!
  BasicBlock::iterator insertp = BB.getFirstInsertionPt();

  BasicBlock::iterator itr = BB.begin();
  if (isa<PHINode>(itr))
    handlePhiNodes(&BB, &env);

  // From this point onwards, nodes cannot be PHI nodes.
  BasicBlock::iterator nextitr;
  for (BasicBlock::iterator itr = insertp; itr != BB.end(); itr = nextitr) {
    nextitr = itr;
    nextitr++;

    // Get static BasicBlock ID: produce bbid
    makeValueId(&BB, env.bbid);
    // Get static instruction ID: produce instid
    getInstId(itr, &env);
    setLineNumberIfExists(itr, &env);

    bool traceCall = true;
    if (CallInst *I = dyn_cast<CallInst>(itr)) {
      Function *fun = I->getCalledFunction();
      // This is an indirect function invocation (i.e. through function
      // pointer). This cannot happen for code that we want to turn into
      // hardware, so skip it. Also, skip intrinsics.
      if (!fun || fun->isIntrinsic())
        continue;
      if (!is_toplevel_mode) {
        std::string callfunc = fun->getName().str();
        traceCall = traceOrNot(callfunc);
        if (!traceCall)
          continue;
      }
    }

    if (isa<CallInst>(itr) && traceCall) {
      handleCallInstruction(itr, &env);
    } else {
      handleNonPhiNonCallInstruction(itr, &env);
    }

    if (!itr->getType()->isVoidTy()) {
      handleInstructionResult(itr, nextitr, &env);
    }

    if (isa<AllocaInst>(itr)) {
      processAllocaInstruction(itr);
    }
  }
  return false;
}


bool Tracer::traceOrNot(std::string& func) {
  if (isTrackedFunction(func))
    return true;
  for (size_t i = 0; i < intrinsics.size(); i++) {
    if (func == intrinsics[i])
      return true;
  }
  return false;
}

bool Tracer::isTrackedFunction(std::string& func) {
  // perform search in log(n) time.
  std::set<std::string>::iterator it = this->tracked_functions.find(func);
  if (it != this->tracked_functions.end()) {
      return true;
  }
  return false;
}

void Tracer::printParamLine(Instruction *I, InstOperandParams *params) {
  printParamLine(I, params->param_num, params->operand_name, params->bbid,
                 params->datatype, params->datasize, params->value,
                 params->is_reg, params->prev_bbid);
}

void Tracer::printParamLine(Instruction *I, int param_num, const char *reg_id,
                            const char *bbId, Type::TypeID datatype,
                            unsigned datasize, Value *value, bool is_reg,
                            const char *prev_bbid) {
  IRBuilder<> IRB(I);
  bool is_phi = (bbId != nullptr && strcmp(bbId, "phi") == 0);
  Value *v_param_num = ConstantInt::get(IRB.getInt64Ty(), param_num);
  Value *v_size = ConstantInt::get(IRB.getInt64Ty(), datasize);
  Value *v_is_reg = ConstantInt::get(IRB.getInt64Ty(), is_reg);
  Value *v_is_phi = ConstantInt::get(IRB.getInt64Ty(), is_phi);
  Constant *vv_reg_id = createStringArg(reg_id, curr_module);
  Constant *vv_prev_bbid = createStringArg(prev_bbid, curr_module);

  if (value != nullptr) {
    if (datatype == llvm::Type::IntegerTyID) {
      Value *v_value = IRB.CreateZExt(value, IRB.getInt64Ty());
      Value *args[] = { v_param_num,    v_size,   v_value,     v_is_reg,
                        vv_reg_id, v_is_phi, vv_prev_bbid };
      IRB.CreateCall(TL_log_int, args);
    } else if (datatype >= llvm::Type::HalfTyID &&
               datatype <= llvm::Type::PPC_FP128TyID) {
      Value *v_value = IRB.CreateFPExt(value, IRB.getDoubleTy());
      Value *args[] = { v_param_num,    v_size,   v_value,     v_is_reg,
                        vv_reg_id, v_is_phi, vv_prev_bbid };
      IRB.CreateCall(TL_log_double, args);
    } else if (datatype == llvm::Type::PointerTyID) {
      Value *v_value = IRB.CreatePtrToInt(value, IRB.getInt64Ty());
      Value *args[] = { v_param_num,    v_size,   v_value,     v_is_reg,
                        vv_reg_id, v_is_phi, vv_prev_bbid };
      IRB.CreateCall(TL_log_int, args);
    } else if (datatype == llvm::Type::VectorTyID) {
      // Give the logger function a pointer to the data. We'll read it out in
      // the logger function itself.
      Value *v_value = createVectorArg(value, IRB);
      Value *args[] = { v_param_num,    v_size,   v_value,     v_is_reg,
                        vv_reg_id, v_is_phi, vv_prev_bbid };
      IRB.CreateCall(TL_log_vector, args);
    } else {
      fprintf(stderr, "normal data else: %d, %s\n", datatype, reg_id);
    }
  } else {
    Value *v_value = ConstantInt::get(IRB.getInt64Ty(), 0);
    Value *args[] = { v_param_num,    v_size,   v_value,     v_is_reg,
                      vv_reg_id, v_is_phi, vv_prev_bbid };
    IRB.CreateCall(TL_log_int, args);
  }
}

void Tracer::printFirstLine(Instruction *I, InstEnv *env, unsigned opcode) {
  IRBuilder<> IRB(I);
  Value *v_opty, *v_linenumber, *v_is_tracked_function,
      *v_is_toplevel_mode;
  if (env->to_fxpt)
    opcode = opcodeToFixedPoint(opcode);
  v_opty = ConstantInt::get(IRB.getInt64Ty(), opcode);
  v_linenumber = ConstantInt::get(IRB.getInt64Ty(), env->line_number);

  // These two parameters are passed so the instrumented binary can be run
  // completely standalone (does not need the WORKLOAD env variable
  // defined).
  v_is_tracked_function = ConstantInt::get(
      IRB.getInt1Ty(),
      (tracked_functions.find(env->funcName) != tracked_functions.end()));
  v_is_toplevel_mode = ConstantInt::get(IRB.getInt1Ty(), is_toplevel_mode);
  Constant *vv_func_name = createStringArg(env->funcName, curr_module);
  Constant *vv_bb = createStringArg(env->bbid, curr_module);
  Constant *vv_inst = createStringArg(env->instid, curr_module);
  Value *args[] = { v_linenumber,      vv_func_name, vv_bb,
                    vv_inst,           v_opty,       v_is_tracked_function,
                    v_is_toplevel_mode };
  IRB.CreateCall(TL_log0, args);
}

unsigned Tracer::opcodeToFixedPoint(unsigned opcode) {
  switch (opcode) {
    case Instruction::FAdd:
      return Instruction::Add;
    case Instruction::FSub:
      return Instruction::Sub;
    case Instruction::FMul:
      return Instruction::Mul;
    case Instruction::FDiv:
      return Instruction::SDiv;
    case Instruction::FRem:
      return Instruction::SRem;
    case Instruction::FPTrunc:
      return Instruction::Trunc;
    case Instruction::FPExt:
      return Instruction::SExt;
    case Instruction::FCmp:
      return Instruction::ICmp;
    default:
      return opcode;
  }
}

bool Tracer::getInstId(Instruction *I, InstEnv* env) {
  return getInstId(I, env->bbid, env->instid, &(env->instc));
}

bool Tracer::setOperandNameAndReg(Instruction *I, InstOperandParams *params) {
  // This instruction operand must have a name or a local slot. If it does not,
  // then it will try to construct an artificial name, which will fail because
  // bbid and instc are NULL.
  params->is_reg = getInstId(I, nullptr, params->operand_name, nullptr);
  return params->is_reg;
}

bool Tracer::getInstId(Instruction *I, char *bbid, char *instid, int *instc) {
  assert(instid != nullptr);
  if (I->hasName()) {
    strcpy(instid, I->getName().str().c_str());
    return true;
  }
  int id = st->getLocalSlot(I);
  if (slotToVarName.find(id) != slotToVarName.end()) {
    strcpy(instid, slotToVarName[id].c_str());
    return true;
  }
  if (id >= 0) {
    sprintf(instid, "%d", id);
    return true;
  }
  if (id == -1) {
    // This instruction does not produce a value in a new register.
    // Examples include branches, stores, calls, returns.
    // instid is constructed using the bbid and a monotonically increasing
    // instruction count.
    assert(bbid != nullptr && instc != nullptr);
    sprintf(instid, "%s-%d", bbid, *instc);
    (*instc)++;
    return true;
  }
  return false;
}

void Tracer::processAllocaInstruction(BasicBlock::iterator it) {
  AllocaInst *alloca = dyn_cast<AllocaInst>(it);
  // If this instruction's output register is already named, then we don't need
  // to do any more searching.
  if (!alloca->hasName()) {
    int alloca_id = st->getLocalSlot(alloca);
    bool found_debug_declare = false;
    // The debug declare call is not guaranteed to come right after the alloca.
    while (!found_debug_declare && !it->isTerminator()) {
      it++;
      Instruction *I = it;
      DbgDeclareInst *di = dyn_cast<DbgDeclareInst>(I);
      if (di) {
        Value *wrapping_arg = di->getAddress();
        // Undefined values do not get a local slot.
        if (isa<UndefValue>(wrapping_arg))
          continue;
        int id = st->getLocalSlot(wrapping_arg);
        // Ensure we've found the RIGHT debug declare call by comparing the
        // variable whose debug information is being declared with the variable
        // we're looking for.
        if (id != alloca_id)
          continue;

        MDNode *md = di->getVariable();
        // The name of the variable is the third operand of the metadata node.
        Value *name_operand = md->getOperand(2);
        std::string name = name_operand->getName().str();
        slotToVarName[id] = name;
        found_debug_declare = true;
      }
    }
  }
}

void Tracer::makeValueId(Value *value, char *id_str) {
  int id = st->getLocalSlot(value);
  bool hasName = value->hasName();
  if (BasicBlock* BB = dyn_cast<BasicBlock>(value)) {
    LoopInfo &info = getAnalysis<LoopInfo>();
    unsigned loop_depth = info.getLoopDepth(BB);
    // 10^3 - 1 is the maximum loop depth.
    const unsigned kMaxLoopDepthChars = 3;
    if (hasName) {
      const std::string& bb_name = value->getName().str();
      snprintf(id_str, bb_name.size() + 1 + kMaxLoopDepthChars, "%s:%u",
               bb_name.c_str(), loop_depth);
    } else {
      snprintf(id_str, 2 * kMaxLoopDepthChars + 1, "%d:%u", id, loop_depth);
    }
    return;
  }
  if (hasName)
    strcpy(id_str, (char *)value->getName().str().c_str());
  if (!hasName && id >= 0)
    sprintf(id_str, "%d", id);
  assert((hasName || id != -1) &&
         "This value does not have a name or a slot number!\n");
}

bool Tracer::isDmaFunction(std::string& funcName) {
  return (funcName == "dmaLoad" ||
          funcName == "dmaStore" ||
          funcName == "dmaFence");
}

void Tracer::setLineNumberIfExists(Instruction *I, InstEnv *env) {
  if (MDNode *N = I->getMetadata("dbg")) {
    DILocation Loc(N); // DILocation is in DebugInfo.h
    env->line_number = Loc.getLineNumber();
  } else {
    env->line_number = -1;
  }
}

// Handle all phi nodes at the beginning of a basic block.
void Tracer::handlePhiNodes(BasicBlock* BB, InstEnv* env) {
  BasicBlock::iterator insertp = BB->getFirstInsertionPt();

  char prev_bbid[InstEnv::BUF_SIZE];
  char operR[InstEnv::BUF_SIZE];

  for (BasicBlock::iterator itr = BB->begin(); isa<PHINode>(itr); itr++) {
    InstOperandParams params;
    params.prev_bbid = prev_bbid;
    params.operand_name = operR;
    params.bbid = s_phi;

    Value *curr_operand = nullptr;

    makeValueId(BB, env->bbid);
    getInstId(itr, env);
    setLineNumberIfExists(itr, env);

    printFirstLine(insertp, env, itr->getOpcode());

    // Print each operand.
    int num_of_operands = itr->getNumOperands();
    if (num_of_operands > 0) {
      for (int i = num_of_operands - 1; i >= 0; i--) {
        BasicBlock *prev_bblock =
            (dyn_cast<PHINode>(itr))->getIncomingBlock(i);
        makeValueId(prev_bblock, params.prev_bbid);
        curr_operand = itr->getOperand(i);
        params.param_num = i + 1;
        params.setDataTypeAndSize(curr_operand);

        if (Instruction *I = dyn_cast<Instruction>(curr_operand)) {
          setOperandNameAndReg(I, &params);
          params.value = nullptr;
          if (!curr_operand->getType()->isVectorTy()) {
            params.setDataTypeAndSize(curr_operand);
          }
        } else {
          params.is_reg = curr_operand->hasName();
          strcpy(params.operand_name, curr_operand->getName().str().c_str());
          if (!curr_operand->getType()->isVectorTy()) {
            params.value = curr_operand;
          }
        }
        printParamLine(insertp, &params);
      }
    }

    // Print result line.
    if (!itr->getType()->isVoidTy()) {
      params.is_reg = true;
      params.param_num = RESULT_LINE;
      params.operand_name = env->instid;
      params.bbid = nullptr;
      params.datatype = itr->getType()->getTypeID();
      params.datasize = getMemSize(itr->getType());
      if (itr->getType()->isVectorTy()) {
        params.value = itr;
      } else if (itr->isTerminator()) {
        assert(false && "It is terminator...\n");
      } else {
        params.value = itr;
      }
      printParamLine(insertp, &params);
    }
  }
}

void Tracer::handleCallInstruction(Instruction* inst, InstEnv* env) {
  char caller_op_name[256];
  char callee_op_name[256];

  CallInst *CI = dyn_cast<CallInst>(inst);
  Function *fun = CI->getCalledFunction();
  strcpy(caller_op_name, (char *)fun->getName().str().c_str());
  unsigned opcode;
  if (fun->getName().str().find("dmaLoad") != std::string::npos)
    opcode = DMA_LOAD;
  else if (fun->getName().str().find("dmaStore") != std::string::npos)
    opcode = DMA_STORE;
  else if (fun->getName().str().find("dmaFence") != std::string::npos)
    opcode = DMA_FENCE;
  else if (fun->getName().str().find("sin") != std::string::npos)
    opcode = SINE;
  else if (fun->getName().str().find("cos") != std::string::npos)
    opcode = COSINE;
  else
    opcode = inst->getOpcode();

  printFirstLine(inst, env, opcode);

  // Print the line that names the function being called.
  int num_operands = inst->getNumOperands();
  Value* func_name_op = inst->getOperand(num_operands - 1);
  InstOperandParams params;
  params.param_num = num_operands;
  params.operand_name = caller_op_name;
  params.bbid = nullptr;
  params.datatype = func_name_op->getType()->getTypeID();
  params.datasize = getMemSize(func_name_op->getType());
  params.value = func_name_op;
  params.is_reg = func_name_op->hasName();
  assert(params.is_reg);
  printParamLine(inst, &params);

  int call_id = 0;
  const Function::ArgumentListType &Args(fun->getArgumentList());
  for (Function::ArgumentListType::const_iterator arg_it = Args.begin(),
                                                  arg_end = Args.end();
       arg_it != arg_end; ++arg_it, ++call_id) {
    Value* curr_operand = inst->getOperand(call_id);

    // Every argument in the function call will have two lines printed,
    // reflecting the state of the operand in the caller AND callee function.
    InstOperandParams caller;
    InstOperandParams callee;

    caller.param_num = call_id + 1;
    caller.operand_name = caller_op_name;
    caller.bbid = nullptr;

    callee.param_num = FORWARD_LINE;
    callee.operand_name = callee_op_name;
    callee.is_reg = true;
    callee.bbid = nullptr;

    caller.setDataTypeAndSize(curr_operand);
    callee.setDataTypeAndSize(curr_operand);
    strcpy(caller.operand_name, curr_operand->getName().str().c_str());
    strcpy(callee.operand_name, arg_it->getName().str().c_str());

    if (Instruction *I = dyn_cast<Instruction>(curr_operand)) {
      // This operand was produced by an instruction in this basic block (and
      // that instruction could be a phi node).
      setOperandNameAndReg(I, &caller);

      // We don't want to print the value of a vector type.
      caller.setDataTypeAndSize(curr_operand);
      callee.setDataTypeAndSize(curr_operand);
      caller.value = curr_operand;
      callee.value = curr_operand;
      printParamLine(inst, &caller);
      printParamLine(inst, &callee);
    } else {
      // This operand was not produced by this basic block. It may be a
      // constant, a local variable produced by a different basic block, a
      // global, a function argument, a code label, or something else.
      caller.is_reg = curr_operand->hasName();
      if (curr_operand->getType()->isVectorTy()) {
        // Nothing to do - again, don't print the value.
        caller.value = curr_operand;
        callee.value = curr_operand;
      } else if (curr_operand->getType()->isLabelTy()) {
        // The operand name should be the code label itself. It has no value.
        makeValueId(curr_operand, caller.operand_name);
        caller.is_reg = true;
      } else if (curr_operand->getValueID() == Value::FunctionVal) {
        // TODO: Replace this with an isa<> check instead.
        // Nothing to do.
      } else {
        // This operand does have a value to print.
        caller.value = curr_operand;
        callee.value = curr_operand;
      }
      printParamLine(inst, &caller);
      printParamLine(inst, &callee);
    }
  }
}

void Tracer::handleNonPhiNonCallInstruction(Instruction *inst, InstEnv* env) {
  char op_name[256];
  printFirstLine(inst, env, inst->getOpcode());
  int num_of_operands = inst->getNumOperands();
  if (num_of_operands > 0) {
    for (int i = num_of_operands - 1; i >= 0; i--) {
      Value* curr_operand = inst->getOperand(i);

      InstOperandParams params;
      params.param_num = i + 1;
      params.operand_name = op_name;
      params.setDataTypeAndSize(curr_operand);
      params.is_reg = curr_operand->hasName();
      strcpy(params.operand_name, curr_operand->getName().str().c_str());

      if (Instruction *I = dyn_cast<Instruction>(curr_operand)) {
        setOperandNameAndReg(I, &params);
        params.value = curr_operand;
      } else {
        if (curr_operand->getType()->isVectorTy()) {
          params.value = curr_operand;
        } else if (curr_operand->getType()->isLabelTy()) {
          makeValueId(curr_operand, params.operand_name);
          params.is_reg = true;
        } else if (curr_operand->getValueID() == Value::FunctionVal) {
          // TODO: Replace this with an isa<> check instead.
          // Nothing more to do.
        } else {
          params.value = curr_operand;
        }
      }
      printParamLine(inst, &params);
    }
  }
}

void Tracer::handleInstructionResult(Instruction *inst, Instruction *next_inst,
                                     InstEnv *env) {
  // Bitcasts are a special instruction in that if the operand is a pointer,
  // the result still refers to the original address. If the result of the
  // bitcast is not named, use the name of the input operand and add a mapping
  // from slot id to name.
  BitCastInst* bitcast = dyn_cast<BitCastInst>(inst);
  if (bitcast) {
    Value* operand = bitcast->getOperand(0);
    int id = st->getLocalSlot(bitcast);
    if (slotToVarName.find(id) == slotToVarName.end() &&
        operand->getType()->isPointerTy()) {
      strcpy(env->instid, operand->getName().str().c_str());
      slotToVarName[id] = env->instid;
    }
  }

  InstOperandParams params;
  params.param_num = RESULT_LINE;
  params.is_reg = true;
  params.operand_name = env->instid;
  params.bbid = nullptr;
  params.setDataTypeAndSize(inst);
  if (inst->getType()->isVectorTy()) {
    params.value = inst;
  } else if (inst->isTerminator()) {
    assert(false && "Return instruction is terminator...\n");
  } else {
    params.value = inst;
  }
  printParamLine(next_inst, &params);
}

void Tracer::getAnalysisUsage(AnalysisUsage& Info) const {
  Info.addRequired<LoopInfo>();
  Info.setPreservesAll();
}

LabelMapHandler::LabelMapHandler() : ModulePass(ID) {}
LabelMapHandler::~LabelMapHandler() {}

bool LabelMapHandler::runOnModule(Module &M) {
    // Since we only want label maps to be added to the trace once at the very
    // start, only instrument the module that contains main().
    Function *main = M.getFunction("main");
    if (!main)
        return false;

    bool ret = readLabelMap();
    if (!ret)
        return false;

    if (verbose)
      errs() << "Contents of labelmap:\n" << labelmap_str << "\n";

    IRBuilder<> builder(main->front().getFirstInsertionPt());
    Function* labelMapWriter = cast<Function>(M.getOrInsertFunction(
        "trace_logger_write_labelmap", builder.getVoidTy(),
        builder.getInt8PtrTy(), builder.getInt64Ty(), nullptr));
    Value *v_size = ConstantInt::get(builder.getInt64Ty(), labelmap_str.length());
    Constant *v_buf = createStringArg(labelmap_str.c_str(), &M);
    Value* args[] = { v_buf, v_size };
    builder.CreateCall(labelMapWriter, args);

    return true;
}

bool LabelMapHandler::readLabelMap() {
    std::ifstream file(labelMapFilename, std::ios::binary | std::ios::ate);
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    if (size > 0) {
        char* labelmap_buf = new char[size+1];
        file.read(labelmap_buf, size);
        labelmap_buf[size] = '\0';
        if (file) {
            labelmap_str = labelmap_buf;
        }
        delete[] labelmap_buf;
    }
    file.close();
    return (labelmap_str.length() != 0);
}

void LabelMapHandler::deleteLabelMap() {
    struct stat buffer;
    if (stat(labelMapFilename.c_str(), &buffer) == 0) {
      std::remove(labelMapFilename.c_str());
    }
}

char Tracer::ID = 0;
char LabelMapHandler::ID = 0;
static RegisterPass<Tracer>
X("fulltrace", "Add full Tracing Instrumentation for Aladdin", false, false);
static RegisterPass<LabelMapHandler>
Y("labelmapwriter", "Read and store label maps into instrumented binary", false, false);

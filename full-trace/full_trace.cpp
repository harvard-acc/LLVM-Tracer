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
#define SET_READY_BITS 95
#define DMA_FENCE 97
#define DMA_STORE 98
#define DMA_LOAD 99
#define SINE 102
#define COSINE 103
#define INTRINSIC 104

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

// This list comprises the set of intrinsic functions we want to have appear in
// the dynamic trace. If we see an intrinsic function, we'll compare its prefix
// with the names here. A match occurs if one of these complete names and the
// first N letters in the intrinsic function name are the same.
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
  "llvm.x86.",  // x86 intrinsics
};

}// end of anonymous namespace

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
      errs() << "[ERROR]: Unknown floating point type " << *T << "\n";
      assert(false);
    }
  } else if (T->isIntegerTy()) {
    size = cast<IntegerType>(T)->getBitWidth();
  } else if (T->isVectorTy()) {
    size = cast<VectorType>(T)->getBitWidth();
  } else if (T->isArrayTy()) {
    ArrayType *A = dyn_cast<ArrayType>(T);
    size = (int)A->getNumElements() *
           A->getElementType()->getPrimitiveSizeInBits();
  } else {
    errs() << "[ERROR]: Unknown data type " << *T << "\n";
    assert(false);
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

  // Add external trace_logger function declarations.
  TL_log_entry = M.getOrInsertFunction("trace_logger_log_entry", VoidTy,
                                       I8PtrTy, I64Ty, nullptr);

  TL_log0 = M.getOrInsertFunction( "trace_logger_log0", VoidTy,
      I64Ty, I8PtrTy, I8PtrTy, I8PtrTy, I64Ty, I1Ty, I1Ty, nullptr);

  TL_log_int = M.getOrInsertFunction( "trace_logger_log_int", VoidTy,
      I64Ty, I64Ty, I64Ty, I64Ty, I8PtrTy, I64Ty, I8PtrTy, nullptr);

  TL_log_ptr = M.getOrInsertFunction( "trace_logger_log_ptr", VoidTy,
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
  bool func_modified = false;

  if (curr_function != &F) {
    st->purgeFunction();
    st->incorporateFunction(&F);
    curr_function = &F;
    slotToVarName.clear();
  }

  // Stack allocated buffers can't be reused across functions of course.
  vector_buffers.clear();

  for (auto bb_it = F.begin(); bb_it != F.end(); ++bb_it) {
    BasicBlock& bb = *bb_it;
    func_modified = runOnBasicBlock(bb);
  }
  if (F.getName() != "main")
    func_modified |= runOnFunctionEntry(F);
  return func_modified;
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
      Function *called_func = I->getCalledFunction();
      // This is an indirect function  invocation (i.e. through called_fun
      // pointer). This cannot happen for code that we want to turn into
      // hardware, so skip it.
      if (!called_func) {
        continue;
      }
      const std::string &called_func_name = called_func->getName().str();
      if (isLLVMIntrinsic(called_func_name) ||
          isDmaFunction(called_func_name)) {
        // There are certain intrinsic functions which represent real work the
        // accelerator may want to do, which we want to capture. These include
        // special math operators, memcpy, and target-specific instructions.
        // Intrinsics we don't want to capture are things like llvm.dbg.*.
        traceCall = true;
      } else if (!is_toplevel_mode) {
        traceCall = traceOrNot(called_func_name);
      } else if (called_func->isIntrinsic()) {
        // Here we capture all the remaining intrinsic functions we DON'T want
        // to trace.
        traceCall = false;
      }
    }
    if (!traceCall)
      continue;

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

  // Conservatively assume that we changed the basic block.
  return true;
}

bool Tracer::runOnFunctionEntry(Function& func) {
  // We have to get the first insertion point before we insert any
  // instrumentation!
  BasicBlock* first_bb = func.begin();
  BasicBlock::iterator insertp = first_bb->getFirstInsertionPt();

  // Fast forward past any vector buffer alloca instructions we find at the
  // beginning of the function block. If we don't do this, we might try to
  // insert instrumentation before an alloca instruction we attempt to reuse.
  for (; insertp != first_bb->end();) {
    if (AllocaInst* alloca = dyn_cast<AllocaInst>(insertp)) {
      if (alloca->hasName() && alloca->getName().startswith("alloca.vecbuf.")) {
        ++insertp;
        continue;
      }
    }
    break;
  }

  Function::ArgumentListType &args(func.getArgumentList());
  std::string funcName = func.getName().str();
  bool is_entry_block = isTrackedFunction(funcName) && is_toplevel_mode;
  if (is_entry_block) {
    InstEnv env;
    strncpy(env.funcName, funcName.c_str(), InstEnv::BUF_SIZE);
    printTopLevelEntryFirstLine(insertp, &env, args.size());
  }

  int call_id = 1;
  for (Function::ArgumentListType::iterator arg_it = args.begin(),
                                            arg_end = args.end();
       arg_it != arg_end; ++arg_it, ++call_id) {
    char arg_name_buf[256];
    InstOperandParams params;
    params.param_num = is_entry_block ? call_id : FORWARD_LINE;
    params.operand_name = arg_name_buf;
    params.setDataTypeAndSize(arg_it);
    params.is_reg = arg_it->hasName();
    params.bbid = nullptr;

    if (arg_it->getType()->isLabelTy()) {
      // The operand name should be the code label itself. It has no value.
      makeValueId(arg_it, params.operand_name);
      params.is_reg = true;
    } else if (arg_it->getValueID() == Value::FunctionVal) {
      // Nothing to do.
    } else {
      // This argument has a value to print.
      strncpy(params.operand_name, arg_it->getName().str().c_str(),
              sizeof(arg_name_buf));
      params.operand_name[255] = 0;
      params.value = arg_it;
    }

    printParamLine(insertp, &params);
  }

  return true;
}

bool Tracer::traceOrNot(const std::string& func) {
  if (isTrackedFunction(func))
    return true;
  return isLLVMIntrinsic(func);
}

bool Tracer::isTrackedFunction(const std::string& func) {
  // perform search in log(n) time.
  std::set<std::string>::iterator it = this->tracked_functions.find(func);
  if (it != this->tracked_functions.end()) {
      return true;
  }
  return false;
}

bool Tracer::isLLVMIntrinsic(const std::string& func) {
  for (size_t i = 0; i < intrinsics.size(); i++) {
    // If the function prefixes match, then we consider it a match.
    if (func.compare(0, intrinsics[i].size(), intrinsics[i]) == 0) {
      return true;
    }
  }
  return false;
}

void Tracer::printParamLine(Instruction *I, InstOperandParams *params) {
  printParamLine(I, params->param_num, params->operand_name, params->bbid,
                 params->datatype, params->datasize, params->value,
                 params->is_reg, params->is_intrinsic, params->prev_bbid);
}

void Tracer::printParamLine(Instruction *I, int param_num, const char *reg_id,
                            const char *bbId, Type::TypeID datatype,
                            unsigned datasize, Value *value, bool is_reg,
                            bool is_intrinsic, const char *prev_bbid) {
  IRBuilder<> IRB(I);
  bool is_phi = (bbId != nullptr && strcmp(bbId, "phi") == 0);
  Value *v_param_num = ConstantInt::get(IRB.getInt64Ty(), param_num);
  Value *v_size = ConstantInt::get(IRB.getInt64Ty(), datasize);
  Value *v_is_reg = ConstantInt::get(IRB.getInt64Ty(), is_reg);
  Value *v_is_phi = ConstantInt::get(IRB.getInt64Ty(), is_phi);
  Constant *vv_reg_id = createStringArgIfNotExists(reg_id);
  Constant *vv_prev_bbid = createStringArgIfNotExists(prev_bbid);

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
      Value *v_value;
      if (is_intrinsic)
        v_value = ConstantInt::get(IRB.getInt64Ty(), 0);
      else
        v_value = IRB.CreatePtrToInt(value, IRB.getInt64Ty());
      Value *args[] = { v_param_num,    v_size,   v_value,     v_is_reg,
                        vv_reg_id, v_is_phi, vv_prev_bbid };
      IRB.CreateCall(TL_log_ptr, args);
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
  Constant *vv_func_name = createStringArgIfNotExists(env->funcName);
  Constant *vv_bb = createStringArgIfNotExists(env->bbid);
  Constant *vv_inst = createStringArgIfNotExists(env->instid);
  Value *args[] = { v_linenumber,      vv_func_name, vv_bb,
                    vv_inst,           v_opty,       v_is_tracked_function,
                    v_is_toplevel_mode };
  IRB.CreateCall(TL_log0, args);
}

void Tracer::printTopLevelEntryFirstLine(Instruction *I, InstEnv *env,
                                         int num_params) {
  IRBuilder<> IRB(I);
  Constant *vv_func_name = createStringArgIfNotExists(env->funcName);
  Value* v_num_params = ConstantInt::get(IRB.getInt64Ty(), num_params);
  Value *args[] = { vv_func_name, v_num_params };
  IRB.CreateCall(TL_log_entry, args);
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

bool Tracer::isDmaFunction(const std::string& funcName) {
  return (funcName == "dmaLoad" ||
          funcName == "dmaStore" ||
          funcName == "dmaFence" ||
          funcName == "setReadyBits");
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
        } else {
          params.is_reg = curr_operand->hasName();
          strcpy(params.operand_name, curr_operand->getName().str().c_str());
          params.value = curr_operand;
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
      if (itr->isTerminator()) {
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

  CallInst *CI = dyn_cast<CallInst>(inst);
  Function *fun = CI->getCalledFunction();
  strcpy(caller_op_name, (char *)fun->getName().str().c_str());
  unsigned opcode;
  if (fun->getName() == "dmaLoad")
    opcode = DMA_LOAD;
  else if (fun->getName() == "dmaStore")
    opcode = DMA_STORE;
  else if (fun->getName() == "dmaFence")
    opcode = DMA_FENCE;
  else if (fun->getName() == "setReadyBits")
    opcode = SET_READY_BITS;
  else if (fun->getName() == "sin")
    opcode = SINE;
  else if (fun->getName() == "cos")
    opcode = COSINE;
  else if (fun->isIntrinsic())
    opcode = INTRINSIC;
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
  params.is_intrinsic = fun->isIntrinsic();
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
    // However, the callee lines will be printed by the callee function itself
    // (after we have entered the function), rather than at the site of the
    // Call instruction.
    InstOperandParams caller;

    caller.param_num = call_id + 1;
    caller.operand_name = caller_op_name;
    caller.bbid = nullptr;

    caller.setDataTypeAndSize(curr_operand);
    strcpy(caller.operand_name, curr_operand->getName().str().c_str());

    if (Instruction *I = dyn_cast<Instruction>(curr_operand)) {
      // This operand was produced by an instruction in this basic block (and
      // that instruction could be a phi node).
      setOperandNameAndReg(I, &caller);

      caller.setDataTypeAndSize(curr_operand);
      caller.value = curr_operand;
      printParamLine(inst, &caller);
    } else {
      // This operand was not produced by this basic block. It may be a
      // constant, a local variable produced by a different basic block, a
      // global, a function argument, a code label, or something else.
      caller.is_reg = curr_operand->hasName();
      if (curr_operand->getType()->isLabelTy()) {
        // The operand name should be the code label itself. It has no value.
        makeValueId(curr_operand, caller.operand_name);
        caller.is_reg = true;
      } else if (curr_operand->getValueID() == Value::FunctionVal) {
        // TODO: Replace this with an isa<> check instead.
        // Nothing to do.
      } else {
        // This operand does have a value to print.
        caller.value = curr_operand;
      }
      printParamLine(inst, &caller);
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
  // bitcast is not named but the input operand is, use the name of the input
  // operand and add a mapping from slot id to name.
  BitCastInst* bitcast = dyn_cast<BitCastInst>(inst);
  if (bitcast) {
    Value* operand = bitcast->getOperand(0);
    int id = st->getLocalSlot(bitcast);
    if (slotToVarName.find(id) == slotToVarName.end() &&
        operand->getType()->isPointerTy() && operand->hasName()) {
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

Constant *Tracer::createStringArgIfNotExists(const char *str) {
  std::string key(str);
  if (global_strings.find(key) == global_strings.end()) {
    global_strings[key] = createStringArg(str, curr_module);
  }
  return global_strings[key];
}

Value *Tracer::createVectorArg(Value *vector, IRBuilder<> &IRB) {
  Type* vector_type = vector->getType();
  assert(vector_type->isVectorTy());
  unsigned vector_size = vector_type->getScalarSizeInBits() / 8;
  AllocaInst* alloca;
  if (vector_buffers.find(vector_size) == vector_buffers.end()) {
    // If we don't find a pre-allocated buffer of this size, we allocate a new
    // one. Critically, we insert the alloca instruction at the very beginning
    // of the function to ensure that it dominates all uses.
    BasicBlock::iterator insertp = curr_function->front().getFirstInsertionPt();
    IRBuilder<> alloca_builder(insertp);
    Value *alloca_size = ConstantInt::get(IRB.getInt64Ty(), 1);
    std::string id = std::to_string(vector_size);
    alloca = alloca_builder.CreateAlloca(vector_type, alloca_size,
                                         "alloca.vecbuf." + id);
    vector_buffers[vector_size] = alloca;
  } else {
    alloca = vector_buffers.at(vector_size);
  }
  IRB.CreateAlignedStore(vector, alloca, vector_size);
  Value *bitcast = IRB.CreatePointerCast(alloca, IRB.getInt8PtrTy());
  return bitcast;
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

    IRBuilder<> builder(main->front().getFirstInsertionPt());
    bool contains_labelmap = readLabelMap();
    if (!contains_labelmap) {
      Function *traceLoggerInit = cast<Function>(M.getOrInsertFunction(
          "trace_logger_init", builder.getVoidTy(), nullptr));
      builder.CreateCall(traceLoggerInit);
    } else {
      if (verbose)
        errs() << "Contents of labelmap:\n" << labelmap_str << "\n";

      Function *labelMapWriter = cast<Function>(M.getOrInsertFunction(
          "trace_logger_write_labelmap", builder.getVoidTy(),
          builder.getInt8PtrTy(), builder.getInt64Ty(), nullptr));
      Value *v_size =
          ConstantInt::get(builder.getInt64Ty(), labelmap_str.length());
      Constant *v_buf = createStringArg(labelmap_str.c_str(), &M);
      Value *args[] = {v_buf, v_size};
      builder.CreateCall(labelMapWriter, args);
    }

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

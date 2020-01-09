#include <vector>
#include <map>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <sys/stat.h>

#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/CommandLine.h"

#include "full_trace.h"

#define RESULT_LINE 19134
#define FORWARD_LINE 24601
#define SET_READY_BITS 95
#define DMA_FENCE 97
#define DMA_STORE 98
#define DMA_LOAD 99
#define SPECIAL_MATH_OP 102
#define INTRINSIC 104
#define SET_SAMPLING_FACTOR 105
#define HOST_STORE 106
#define HOST_LOAD 107

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

void split(const std::string &s, const char delim,
           std::set<std::string> &elems) {
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

std::vector<std::string> special_math_ops = {
  "acos",
  "asin",
  "atan",
  "atan2",
  "cos",
  "cosh",
  "sin",
  "sinh",
  "tanh",
  "exp",
  "frexp",
  "ldexp",
  "log",
  "log10",
  "modf",
  "pow",
  "sqrt",
  "ceil",
  "fabs",
  "floor",
  "fmod",
};

}  // end of anonymous namespace

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
    return ConstantExpr::getGetElementPtr(ArrayTy_0, gvar_array, indices);
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
  std::set<std::string> user_workloads = getUserWorkloadFunctions();
  if (user_workloads.empty()) {
    errs() << "\n\nPlease set WORKLOAD as an environment variable!\n\n\n";
    return false;
  }

  auto &llvm_context = M.getContext();
  auto I1Ty = Type::getInt1Ty(llvm_context);
  auto I64Ty = Type::getInt64Ty(llvm_context);
  auto I8PtrTy = Type::getInt8PtrTy(llvm_context);
  auto VoidTy = Type::getVoidTy(llvm_context);
  auto DoubleTy = Type::getDoubleTy(llvm_context);

  // Add external trace_logger function declarations.
  TL_log_entry = M.getOrInsertFunction("trace_logger_log_entry", VoidTy,
                                       I8PtrTy, I64Ty);

  TL_log0 = M.getOrInsertFunction( "trace_logger_log0", VoidTy,
      I64Ty, I8PtrTy, I8PtrTy, I8PtrTy, I64Ty, I1Ty, I1Ty);

  TL_log_int = M.getOrInsertFunction( "trace_logger_log_int", VoidTy,
      I64Ty, I64Ty, I64Ty, I64Ty, I8PtrTy, I64Ty, I8PtrTy);

  TL_log_ptr = M.getOrInsertFunction( "trace_logger_log_ptr", VoidTy,
      I64Ty, I64Ty, I64Ty, I64Ty, I8PtrTy, I64Ty, I8PtrTy);

  TL_log_string =
      M.getOrInsertFunction("trace_logger_log_string", VoidTy, I64Ty, I64Ty,
                            I8PtrTy, I64Ty, I8PtrTy, I64Ty, I8PtrTy);

  TL_log_double = M.getOrInsertFunction( "trace_logger_log_double", VoidTy,
      I64Ty, I64Ty, DoubleTy, I64Ty, I8PtrTy, I64Ty, I8PtrTy);

  TL_log_vector = M.getOrInsertFunction( "trace_logger_log_vector", VoidTy,
      I64Ty, I64Ty, I8PtrTy, I64Ty, I8PtrTy, I64Ty, I8PtrTy);

  TL_update_status = M.getOrInsertFunction("trace_logger_update_status", VoidTy,
                                           I8PtrTy, I64Ty, I1Ty, I1Ty);

  // We will instrument in top level mode if there is only one workload
  // function or if explicitly told to do so.
  is_toplevel_mode = (user_workloads.size() == 1) || traceAllCallees;
  if (is_toplevel_mode && verbose)
    std::cout << "LLVM-Tracer is instrumenting this workload in top-level mode.\n";

  curr_module = &M;
  curr_function = nullptr;
  debugInfoFinder.processModule(M);

  auto it = debugInfoFinder.subprograms().begin();
  auto eit = debugInfoFinder.subprograms().end();

  for (auto i = it; i != eit; ++i) {
    DISubprogram* const S = (*i);
    StringRef mangledName = S->getLinkageName();
    StringRef name = S->getName();

    assert(name.size() || mangledName.size());
    if (!mangledName.empty()) {
      mangledNameMap[mangledName] = name;
    } else {
      mangledNameMap[name] = name;
    }

    // Checks out whether Name or Mangled Name matches.
    auto MangledIt = user_workloads.find(mangledName);
    bool isMangledMatch = MangledIt != user_workloads.end();

    auto PreMangledIt = user_workloads.find(name);
    bool isPreMangledMatch = PreMangledIt != user_workloads.end();

    if (isMangledMatch | isPreMangledMatch) {
      if (mangledName.empty()) {
        this->tracked_functions.insert(name);
      } else {
        this->tracked_functions.insert(mangledName);
      }
    }
  }

  return false;
}

std::set<std::string> Tracer::getUserWorkloadFunctions() const {
  std::set<std::string> user_workloads;
  char* workload = getenv("WORKLOAD");
  if (workload) {
      std::string func_string = workload;
      split(func_string, ',', user_workloads);
  }
  return user_workloads;
}

bool Tracer::runOnFunction(Function &F) {
  // The tracer only supports C code, not C++, so if there is a mangled name
  // that differs from the canonical name, then it must be a C++ function that
  // should be skipped.
  StringRef mangledName = F.getName();
  auto it = mangledNameMap.find(mangledName);
  if (it == mangledNameMap.end() || it->second != mangledName)
    return false;

  bool func_modified = false;
  curr_function = &F;
  st = new ModuleSlotTracker(curr_module);
  st->incorporateFunction(F);
  slotToVarName.clear();
  // Stack allocated buffers can't be reused across functions of course.
  vector_buffers.clear();

  // Collect debug info before adding any instrumentation.
  //
  // First, collect all debug information in this function. If value names have
  // been discarded in the LLVM IR, we can use this debug info to recover value
  // names.
  for (auto bb_it = F.begin(); bb_it != F.end(); ++bb_it) {
    for (auto inst_it = bb_it->begin(); inst_it != bb_it->end(); ++inst_it) {
      if (DbgInfoIntrinsic *debug = dyn_cast<DbgInfoIntrinsic>(inst_it))
        collectDebugInfo(debug);
    }
  }

  // Collect all preheader branch instructions (the one at the end of a
  // preheader block). Use the loop's start location as the preheader's line
  // num.
  LoopInfo &LI = getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
  for (auto bb_it = F.begin(); bb_it != F.end(); ++bb_it) {
    if (Loop *loop = LI.getLoopFor(&*bb_it))
      if (BasicBlock *PHeadBB = loop->getLoopPreheader())
        if (Instruction *PHeadInst = PHeadBB->getTerminator())
          preheaderLineNum[PHeadInst] = loop->getStartLoc().getLine();
  }

  for (auto bb_it = F.begin(); bb_it != F.end(); ++bb_it) {
    BasicBlock& bb = *bb_it;
    func_modified = runOnBasicBlock(bb);
  }
  if (F.getName() != "main")
    func_modified |= runOnFunctionEntry(F);

  purgeDebugInfo();
  delete st;
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

  if (isDmaFunction(funcName) || isHostMemFunction(funcName) ||
      isSetSamplingFactor(funcName))
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

    // Invoke instructions are used to call functions that may throw exceptions.
    // They are the only the terminator instruction that can also return a
    // value. LLVM-Tracer only supports instrumentation of C code (although that
    // code can live in a C++ file). This kind of behavior is thus unsupported
    // and does not need any instrumentation.
    if (isa<InvokeInst>(*itr))
      continue;

    // Get static BasicBlock ID: produce bbid
    makeValueId(&BB, env.bbid);
    // Get static instruction ID: produce instid
    Instruction* currInst = cast<Instruction>(itr);
    getInstId(currInst, &env);
    setLineNumberIfExists(currInst, &env);

    bool traceCall = true;
    if (CallInst *I = dyn_cast<CallInst>(currInst)) {
      Function *called_func = I->getCalledFunction();
      // This is an indirect function  invocation (i.e. through called_fun
      // pointer). This cannot happen for code that we want to turn into
      // hardware, so skip it.
      if (!called_func) {
        continue;
      }
      const std::string &called_func_name = called_func->getName().str();
      if (isLLVMIntrinsic(called_func_name) ||
          isDmaFunction(called_func_name) ||
          isHostMemFunction(called_func_name) ||
          isSetSamplingFactor(called_func_name)) {
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

    if (isa<CallInst>(currInst) && traceCall) {
      handleCallInstruction(currInst, &env);
    } else {
      handleNonPhiNonCallInstruction(currInst, &env);
    }

    if (!currInst->getType()->isVoidTy()) {
      Instruction* nextInst = cast<Instruction>(nextitr);
      handleInstructionResult(currInst, nextInst, &env);
    }

    if (isa<AllocaInst>(currInst)) {
      processAllocaInstruction(itr);
    }
  }

  // Conservatively assume that we changed the basic block.
  return true;
}

bool Tracer::runOnFunctionEntry(Function& func) {
  // We have to get the first insertion point before we insert any
  // instrumentation!
  BasicBlock* first_bb = cast<BasicBlock>(func.begin());
  BasicBlock::iterator insertp = first_bb->getFirstInsertionPt();

  // Fast forward past any vector buffer alloca instructions we find at the
  // beginning of the function block. If we don't do this, we might try to
  // insert instrumentation before an alloca instruction we attempt to reuse.
  for (; insertp != first_bb->end();) {
    if (AllocaInst* alloca = dyn_cast<AllocaInst>(insertp)) {
      Type* allocated_type = alloca->getAllocatedType();
      if (allocated_type->isVectorTy()) {
        // There can only be one alloca instruction for a given vector type in
        // our vector buffer, so see if that one is equal to the current alloca
        // inst. If so, then don't instrument this instruction.
        VecBufKey key = createVecBufKey(allocated_type);
        auto buf_it = vector_buffers.find(key);
        if (buf_it != vector_buffers.end() && buf_it->second == alloca) {
          ++insertp;
          continue;
        }
      }
    }
    break;
  }

  Instruction *insertPointInst = cast<Instruction>(insertp);
  std::string funcName = func.getName().str();
  bool is_entry_block = isTrackedFunction(funcName) && is_toplevel_mode;
  if (is_entry_block) {
    InstEnv env;
    strncpy(env.funcName, funcName.c_str(), InstEnv::BUF_SIZE);
    updateTracerStatus(insertPointInst, &env, 0);
    printTopLevelEntryFirstLine(insertPointInst, &env, func.arg_size());
  }

  int call_id = 1;
  for (auto arg_it = func.arg_begin(); arg_it != func.arg_end();
       ++arg_it, ++call_id) {
    char arg_name_buf[256];
    ValueNameLookup valueName = getValueName(arg_it);
    InstOperandParams params;
    params.param_num = is_entry_block ? call_id : FORWARD_LINE;
    params.operand_name = arg_name_buf;
    params.setDataTypeAndSize(arg_it);
    params.is_reg = valueName.first;
    params.bbid = nullptr;

    if (arg_it->getType()->isLabelTy()) {
      // The operand name should be the code label itself. It has no value.
      makeValueId(arg_it, params.operand_name);
      params.is_reg = true;
    } else if (arg_it->getValueID() == Value::FunctionVal) {
      // Nothing to do.
    } else {
      // This argument has a value to print.
      strncpy(params.operand_name, valueName.second.str().c_str(),
              sizeof(arg_name_buf));
      params.operand_name[255] = 0;
      params.value = arg_it;
    }

    printParamLine(insertPointInst, &params);
  }

  return true;
}

void Tracer::collectDebugInfo(DbgInfoIntrinsic *debug) {
  Value *arg = nullptr;
  DILocalVariable *var = nullptr;
  if (DbgDeclareInst *dbgDeclare = dyn_cast<DbgDeclareInst>(debug)) {
    arg = dbgDeclare->getAddress();
    var = dbgDeclare->getVariable();
    if (isa<UndefValue>(arg) || !var)
      return;
  } else if (DbgValueInst *dbgValue = dyn_cast<DbgValueInst>(debug)) {
    arg = dbgValue->getValue();
    var = dbgValue->getVariable();
    if (isa<UndefValue>(arg) || !var)
      return;
  } else if (DbgAddrIntrinsic *dbgAddr = dyn_cast<DbgAddrIntrinsic>(debug)) {
    arg = dbgDeclare->getAddress();
    var = dbgAddr->getVariable();
    if (isa<UndefValue>(arg) || !var)
      return;
  } else {
      return;
  }
  // Aladdin does not support renamed values. All values, particularly
  // pointers, must be addressed by their original name in the function
  // signature. As a result, a pointer rename (e.g. via a cast) needs to be
  // ignored.
  if (isa<BitCastInst>(arg))
      return;
  if (valueDebugName.find(arg) == valueDebugName.end()) {
    valueDebugName[arg] = var->getName();
  }
}

void Tracer::purgeDebugInfo() {
  valueDebugName.clear();
  preheaderLineNum.clear();
}

Tracer::ValueNameLookup Tracer::getValueName(Value *value) {
  if (value->hasName())
    return std::make_pair(true, value->getName());
  auto it = valueDebugName.find(value);
  if (it != valueDebugName.end())
    return std::make_pair(true, it->second);
  return std::make_pair(false, StringRef());
}

bool Tracer::traceOrNot(const std::string& func) {
  if (isTrackedFunction(func))
    return true;
  return isLLVMIntrinsic(func);
}

bool Tracer::isTrackedFunction(const std::string& func) {
  // perform search in log(n) time.
  std::set<StringRef>::iterator it = this->tracked_functions.find(func);
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

bool Tracer::isSpecialMathOp(const std::string& func) {
  for (size_t i = 0; i < special_math_ops.size(); i++) {
    if (func.compare(special_math_ops[i]) == 0) {
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
      Value *v_value = nullptr;
      Type *pteetype = value->getType()->getPointerElementType();
      bool is_string = false;
      if (is_intrinsic) {
        v_value = ConstantInt::get(IRB.getInt64Ty(), 0);
      } else {
        v_value = IRB.CreatePtrToInt(value, IRB.getInt64Ty());
        // If the pointee type is 8-bit integer, get the string.
        // TODO: the code below only works for constant expressio strings, but
        // not for mutable strings.
        if (IntegerType *itype = dyn_cast<IntegerType>(pteetype)) {
          if (itype->getBitWidth() == 8) {
            if (ConstantExpr *ce = dyn_cast<ConstantExpr>(value)) {
              if (GlobalVariable *gv =
                      dyn_cast<GlobalVariable>(ce->getOperand(0))) {
                if (ConstantDataArray *array =
                        dyn_cast<ConstantDataArray>(gv->getInitializer())) {
                  v_value =
                      createStringArgIfNotExists(array->getAsCString().data());
                  is_string = true;
                }
              }
            }
          }
        }
      }
      Value *args[] = { v_param_num, v_size,   v_value,     v_is_reg,
                        vv_reg_id,   v_is_phi, vv_prev_bbid };
      if (is_string)
        IRB.CreateCall(TL_log_string, args);
      else
        IRB.CreateCall(TL_log_ptr, args);
    } else if (datatype == llvm::Type::VectorTyID) {
      // Give the logger function a pointer to the data. We'll read it out in
      // the logger function itself.
      Value *v_value = createVectorArg(value, IRB);
      Value *args[] = { v_param_num,    v_size,   v_value,     v_is_reg,
                        vv_reg_id, v_is_phi, vv_prev_bbid };
      IRB.CreateCall(TL_log_vector, args);
    } else {
      errs() << "[WARNING]: Encountered unhandled datatype ";
      if (datatype == Type::FunctionTyID) {
        errs() << "FunctionType";
      } else if (datatype == Type::StructTyID) {
        errs() << "StructType";
      } else if (datatype == Type::ArrayTyID) {
        errs() << "ArrayType";
      } else if (datatype == Type::TokenTyID) {
        errs() << "ArrayType";
      } else {
        Type* t = Type::getPrimitiveType(curr_module->getContext(), datatype);
        errs() << *t;
      }
      errs() << " on variable " << reg_id << "\n";
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

void Tracer::updateTracerStatus(Instruction *I, InstEnv *env, int opcode) {
  IRBuilder<> IRB(I);
  Constant *func_name = createStringArgIfNotExists(env->funcName);
  if (env->to_fxpt)
    opcode = opcodeToFixedPoint(opcode);
  Value *v_opcode = ConstantInt::get(IRB.getInt64Ty(), opcode);
  Value *v_is_tracked_function = ConstantInt::get(
      IRB.getInt1Ty(),
      (tracked_functions.find(env->funcName) != tracked_functions.end()));
  Value *v_is_toplevel_mode =
      ConstantInt::get(IRB.getInt1Ty(), is_toplevel_mode);
  Value *args[] = {func_name, v_opcode, v_is_tracked_function,
                   v_is_toplevel_mode};
  IRB.CreateCall(TL_update_status, args);
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
  ValueNameLookup name = getValueName(I);
  if (name.first) {
    strcpy(instid, name.second.str().c_str());
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
  ValueNameLookup name = getValueName(alloca);
  if (!name.first) {
    int alloca_id = st->getLocalSlot(alloca);
    bool found_debug_declare = false;
    // The debug declare call is not guaranteed to come right after the alloca.
    while (!found_debug_declare && !it->isTerminator()) {
      it++;
      Instruction *I = cast<Instruction>(it);
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

        DILocalVariable* name_operand = di->getVariable();
        std::string name = name_operand->getName().str();
        slotToVarName[id] = name;
        found_debug_declare = true;
      }
    }
  }
}

void Tracer::makeValueId(Value *value, char *id_str) {
  int id = st->getLocalSlot(value);
  ValueNameLookup name = getValueName(value);
  bool hasName = name.first;
  if (BasicBlock* BB = dyn_cast<BasicBlock>(value)) {
    LoopInfo &info = getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
    unsigned loop_depth = info.getLoopDepth(BB);
    // 10^3 - 1 is the maximum loop depth.
    const unsigned kMaxLoopDepthChars = 3;
    if (hasName) {
      const std::string& bb_name = name.second.str();
      snprintf(id_str, bb_name.size() + 1 + kMaxLoopDepthChars, "%s:%u",
               bb_name.c_str(), loop_depth);
    } else {
      snprintf(id_str, 2 * kMaxLoopDepthChars + 1, "%d:%u", id, loop_depth);
    }
    return;
  }
  if (hasName)
    strcpy(id_str, (char *)name.second.str().c_str());
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

bool Tracer::isHostMemFunction(const std::string& funcName) {
  return (funcName == "hostLoad" || funcName == "hostStore");
}

bool Tracer::isSetSamplingFactor(const std::string &funcName) {
  return funcName == "setSamplingFactor";
}

void Tracer::setLineNumberIfExists(Instruction *I, InstEnv *env) {
  // If this instruction is a preheader branch, use the recorded loop starting
  // location as its line number.
  if (preheaderLineNum.find(I) != preheaderLineNum.end()) {
    env->line_number = preheaderLineNum[I];
    return;
  }
  // Otherwise, use debug info.
  if (MDNode *N = I->getMetadata("dbg")) {
    DILocation* loc = dyn_cast<DILocation>(N);
    if (loc) {
      env->line_number = loc->getLine();
      return;
    }
  }
  env->line_number = -1;
}

// Handle all phi nodes at the beginning of a basic block.
void Tracer::handlePhiNodes(BasicBlock* BB, InstEnv* env) {
  BasicBlock::iterator insertp = BB->getFirstInsertionPt();
  Instruction* insertPointInst = cast<Instruction>(insertp);

  char prev_bbid[InstEnv::BUF_SIZE];
  char operR[InstEnv::BUF_SIZE];

  for (BasicBlock::iterator itr = BB->begin(); isa<PHINode>(itr); itr++) {
    Instruction* currInst = cast<Instruction>(itr);
    InstOperandParams params;
    params.prev_bbid = prev_bbid;
    params.operand_name = operR;
    params.bbid = s_phi;

    Value *curr_operand = nullptr;

    makeValueId(BB, env->bbid);
    getInstId(currInst, env);
    setLineNumberIfExists(currInst, env);

    printFirstLine(insertPointInst, env, currInst->getOpcode());

    // Print each operand.
    int num_of_operands = currInst->getNumOperands();
    if (num_of_operands > 0) {
      for (int i = num_of_operands - 1; i >= 0; i--) {
        BasicBlock *prev_bblock =
            (dyn_cast<PHINode>(currInst))->getIncomingBlock(i);
        makeValueId(prev_bblock, params.prev_bbid);
        curr_operand = currInst->getOperand(i);
        params.param_num = i + 1;
        params.setDataTypeAndSize(curr_operand);

        if (Instruction *I = dyn_cast<Instruction>(curr_operand)) {
          setOperandNameAndReg(I, &params);
          params.value = nullptr;
        } else {
          ValueNameLookup name = getValueName(curr_operand);
          params.is_reg = name.first;
          strcpy(params.operand_name, name.second.str().c_str());
          params.value = curr_operand;
        }
        printParamLine(insertPointInst, &params);
      }
    }

    // Print result line.
    if (!currInst->getType()->isVoidTy()) {
      params.is_reg = true;
      params.param_num = RESULT_LINE;
      params.operand_name = env->instid;
      params.bbid = nullptr;
      params.datatype = currInst->getType()->getTypeID();
      params.datasize = getMemSize(currInst->getType());
      if (currInst->isTerminator()) {
        assert(false && "It is terminator...\n");
      } else {
        params.value = currInst;
      }
      printParamLine(insertPointInst, &params);
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
  else if (isSpecialMathOp(fun->getName().str()))
    opcode = SPECIAL_MATH_OP;
  else if (fun->getName() == "setSamplingFactor")
    opcode = SET_SAMPLING_FACTOR;
  else if (fun->getName() == "hostLoad")
    opcode = HOST_LOAD;
  else if (fun->getName() == "hostStore")
    opcode = HOST_STORE;
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
  for (auto arg_it = fun->arg_begin(); arg_it != fun->arg_end();
       ++arg_it, ++call_id) {
    Value* curr_operand = inst->getOperand(call_id);
    ValueNameLookup name = getValueName(curr_operand);

    // Every argument in the function call will have two lines printed,
    // reflecting the state of the operand in the caller AND callee function.
    // However, the callee lines will be printed by the callee function itself
    // (after we have entered the function), rather than at the site of the
    // Call instruction.
    InstOperandParams caller;

    caller.param_num = call_id + 1;
    caller.operand_name = caller_op_name;
    caller.bbid = nullptr;
    caller.is_intrinsic = params.is_intrinsic;

    caller.setDataTypeAndSize(curr_operand);
    strcpy(caller.operand_name, name.second.str().c_str());

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
      caller.is_reg = name.first;
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
      ValueNameLookup name = getValueName(curr_operand);
      params.is_reg = name.first;
      strcpy(params.operand_name, name.second.str().c_str());

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
  if (isa<ReturnInst>(inst)) {
    updateTracerStatus(inst, env, inst->getOpcode());
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
    ValueNameLookup name = getValueName(operand);
    int id = st->getLocalSlot(bitcast);
    if (slotToVarName.find(id) == slotToVarName.end() &&
        operand->getType()->isPointerTy() && name.first) {
      strcpy(env->instid, name.second.str().c_str());
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
    errs() << "Encountered an unexpected terminator instruction!\n"
           << *inst << "\n";
    assert(false);
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

Tracer::VecBufKey Tracer::createVecBufKey(Type* vector_type) {
  assert(vector_type->isVectorTy());
  unsigned num_elements = vector_type->getVectorNumElements();
  unsigned scalar_size = vector_type->getScalarSizeInBits() / 8;
  Type::TypeID element_type = vector_type->getVectorElementType()->getTypeID();
  VecBufKey key = std::make_tuple(num_elements, scalar_size, element_type);
  return key;
}

Value *Tracer::createVectorArg(Value *vector, IRBuilder<> &IRB) {
  Type* vector_type = vector->getType();
  VecBufKey key = createVecBufKey(vector_type);
  AllocaInst* alloca;
  if (vector_buffers.find(key) == vector_buffers.end()) {
    // If we don't find a pre-allocated buffer of this size, we allocate a new
    // one. Critically, we insert the alloca instruction at the very beginning
    // of the function to ensure that it dominates all uses.
    Instruction *insertp =
        cast<Instruction>(curr_function->front().getFirstInsertionPt());
    IRBuilder<> alloca_builder(insertp);
    Value *alloca_size = ConstantInt::get(IRB.getInt64Ty(), 1);
    alloca = alloca_builder.CreateAlloca(vector_type, alloca_size);
    vector_buffers[key] = alloca;
  } else {
    alloca = vector_buffers.at(key);
  }
  IRB.CreateAlignedStore(vector, alloca, std::get<1>(key));
  Value *bitcast = IRB.CreatePointerCast(alloca, IRB.getInt8PtrTy());
  return bitcast;
}

void Tracer::getAnalysisUsage(AnalysisUsage& Info) const {
  Info.addRequired<LoopInfoWrapperPass>();
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

    IRBuilder<> builder(cast<Instruction>(main->front().getFirstInsertionPt()));
    Function *traceLoggerInit = cast<Function>(
        M.getOrInsertFunction("trace_logger_init", builder.getVoidTy()));
    builder.CreateCall(traceLoggerInit);
    bool contains_labelmap = readLabelMap();
    if (contains_labelmap) {
      if (verbose)
        errs() << "Contents of labelmap:\n" << labelmap_str << "\n";

      Function *labelMapRegister = cast<Function>(M.getOrInsertFunction(
          "trace_logger_register_labelmap", builder.getVoidTy(),
          builder.getInt8PtrTy(), builder.getInt64Ty()));
      Value *v_size =
          ConstantInt::get(builder.getInt64Ty(), labelmap_str.length());
      Constant *v_buf = createStringArg(labelmap_str.c_str(), &M);
      Value *args[] = { v_buf, v_size };
      builder.CreateCall(labelMapRegister, args);
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

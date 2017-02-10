#include <vector>
#include <map>
#include <cmath>
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Type.h"
#include "llvm/DebugInfo.h"
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
                                 cl::desc("Specify labelmap filename"),
                                 cl::value_desc("filename"),
                                 cl::init("labelmap"));
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

Tracer::Tracer() : BasicBlockPass(ID) {}

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

  if (func_string.empty()) {
    errs() << "\n\nPlease set WORKLOAD as an environment variable!\n\n\n";
    return false;
  }
  std::set<std::string> user_workloads;
  split(func_string, ',', user_workloads);
  // Set toplevel function tracking mode if only one function is specified.
  is_toplevel_mode = (user_workloads.size() == 1);
  if (is_toplevel_mode)
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

bool Tracer::runOnBasicBlock(BasicBlock &BB) {
  Function *func = BB.getParent();
  int instc = 0;
  std::string funcName = func->getName().str();

  if (curr_function != func) {
    st->purgeFunction();
    st->incorporateFunction(func);
    curr_function = func;
  }

  if (!is_toplevel_mode && !is_tracking_function(funcName))
    return false;

  if (is_dma_function(funcName))
    return false;

  std::cout << "Tracking function: " << funcName << std::endl;
  // We have to get the first insertion point before we insert any
  // instrumentation!
  BasicBlock::iterator insertp = BB.getFirstInsertionPt();

  BasicBlock::iterator itr = BB.begin();
  if (isa<PHINode>(itr))
    handlePhiNodes(&BB, instc, func);

  InstEnv env;
  strncpy(env.funcName, funcName.c_str(), InstEnv::BUF_SIZE);

  // From this point onwards, nodes cannot be PHI nodes.
  BasicBlock::iterator nextitr;
  for (BasicBlock::iterator itr = insertp; itr != BB.end(); itr = nextitr) {
    nextitr = itr;
    nextitr++;

    // Get static BasicBlock ID: produce bbid
    getBBId(&BB, env.bbid);
    // Get static instruction ID: produce instid
    getInstId(itr, env.bbid, env.instid, env.instc);

    if (MDNode *N = itr->getMetadata("dbg")) {
      DILocation Loc(N); // DILocation is in DebugInfo.h
      env.line_number = Loc.getLineNumber();
    } else {
      env.line_number = -1;
    }

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
        traceCall = trace_or_not(callfunc);
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
  }
  return false;
}


bool Tracer::trace_or_not(std::string& func) {
  if (is_tracking_function(func))
    return true;
  for (int i = 0; i < intrinsics.size(); i++) {
    if (func == intrinsics[i])
      return true;
  }
  return false;
}

bool Tracer::is_tracking_function(std::string& func) {
  // perform search in log(n) time.
  std::set<std::string>::iterator it = this->tracked_functions.find(func);
  if (it != this->tracked_functions.end()) {
      return true;
  }
  return false;
}

int Tracer::getMemSize(Type *T) {
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

void Tracer::printParamLine(Instruction *I, int param_num, const char *reg_id,
                            const char *bbId, Type::TypeID datatype,
                            unsigned datasize, Value *value, bool is_reg,
                            const char *prev_bbid, bool is_phi) {
  // Print parameter/result line.

  if (bbId != nullptr && strcmp(bbId, "phi") == 0)
    is_phi = true;
  IRBuilder<> IRB(I);
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

bool Tracer::getInstId(Instruction *itr, char *bbid, char *instid, int &instc) {
  int id = st->getLocalSlot(itr);
  bool has_name = itr->hasName();
  if (has_name) {
    strcpy(instid, (char *)itr->getName().str().c_str());
    return true;
  }
  if (!has_name && id >= 0) {
    sprintf(instid, "%d", id);
    return true;
  } else if (!has_name && id == -1) {
    char tmp[10];
    char dash[5] = "-";
    sprintf(tmp, "%d", instc);
    if (bbid != nullptr)
      strcpy(instid, bbid);
    strcat(instid, dash);
    strcat(instid, tmp);
    instc++;
    return true;
  }
  return false;
}

void Tracer::getBBId(Value *BB, char *bbid) {
  int id;
  id = st->getLocalSlot(BB);
  bool hasName = BB->hasName();
  if (hasName)
    strcpy(bbid, (char *)BB->getName().str().c_str());
  if (!hasName && id >= 0)
    sprintf(bbid, "%d", id);
  else if (!hasName && id == -1)
    fprintf(stderr, "!!This block does not have a name or a ID!\n");
}

bool Tracer::is_dma_function(std::string& funcName) {
  return (funcName == "dmaLoad" ||
          funcName == "dmaStore" ||
          funcName == "dmaFence");
}

// Handle all phi nodes at the beginning of a basic block.
void Tracer::handlePhiNodes(BasicBlock* BB, int& instc, Function* func) {
  BasicBlock::iterator insertp = BB->getFirstInsertionPt();
  std::string funcName = BB->getParent()->getName().str();

  InstEnv env;
  strncpy(env.funcName, funcName.c_str(), InstEnv::BUF_SIZE);
  for (BasicBlock::iterator itr = BB->begin(); isa<PHINode>(itr); itr++) {
    Value *curr_operand = nullptr;
    bool is_reg = false;
    char instid[256], operR[256];

    getBBId(BB, env.bbid);
    getInstId(itr, env.bbid, env.instid, env.instc);

    if (MDNode *N = itr->getMetadata("dbg")) {
      DILocation Loc(N); // DILocation is in DebugInfo.h
      env.line_number = Loc.getLineNumber();
    }
    printFirstLine(insertp, &env, itr->getOpcode());

    int num_of_operands = itr->getNumOperands();

    /*Print each operand*/
    if (num_of_operands > 0) {
      for (int i = num_of_operands - 1; i >= 0; i--) {

        BasicBlock *prev_bblock =
            (dyn_cast<PHINode>(itr))->getIncomingBlock(i);
        char prev_bbid[256];
        getBBId(prev_bblock, prev_bbid);
        curr_operand = itr->getOperand(i);
        is_reg = curr_operand->hasName();

        if (Instruction *I = dyn_cast<Instruction>(curr_operand)) {
          int flag = 0;
          is_reg = getInstId(I, nullptr, operR, flag);
          assert(flag == 0);
          if (curr_operand->getType()->isVectorTy()) {
            printParamLine(insertp, i + 1, operR, s_phi,
                           curr_operand->getType()->getTypeID(),
                           getMemSize(curr_operand->getType()), nullptr, is_reg,
                           prev_bbid);
          } else {
            printParamLine(insertp, i + 1, operR, s_phi,
                           I->getType()->getTypeID(), getMemSize(I->getType()),
                           nullptr, is_reg, prev_bbid);
          }
        } else if (curr_operand->getType()->isVectorTy()) {
          char operand_id[256];
          strcpy(operand_id, curr_operand->getName().str().c_str());
          printParamLine(insertp, i + 1, operand_id, s_phi,
                         curr_operand->getType()->getTypeID(),
                         getMemSize(curr_operand->getType()), nullptr, is_reg,
                         prev_bbid);
        } else {
          char operand_id[256];
          strcpy(operand_id, curr_operand->getName().str().c_str());
          printParamLine(insertp, i + 1, operand_id, s_phi,
                         curr_operand->getType()->getTypeID(),
                         getMemSize(curr_operand->getType()), curr_operand,
                         is_reg, prev_bbid);
        }
      }
    }
    // Print result line.
    if (!itr->getType()->isVoidTy()) {
      is_reg = 1;
      if (itr->getType()->isVectorTy()) {
        printParamLine(insertp, RESULT_LINE, env.instid, nullptr,
                       itr->getType()->getTypeID(), getMemSize(itr->getType()),
                       nullptr, is_reg);
      } else if (itr->isTerminator())
        fprintf(stderr, "It is terminator...\n");
      else {
        printParamLine(insertp, RESULT_LINE, env.instid, nullptr,
                       itr->getType()->getTypeID(), getMemSize(itr->getType()),
                       itr, is_reg);
      }
    }
  }
}

void Tracer::handleCallInstruction(Instruction* inst, InstEnv* env) {
  char operR[256];

  CallInst *CI = dyn_cast<CallInst>(inst);
  Function *fun = CI->getCalledFunction();
  strcpy(operR, (char *)fun->getName().str().c_str());
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

  int num_of_operands = inst->getNumOperands();
  Value* curr_operand = inst->getOperand(num_of_operands - 1);
  bool is_reg = curr_operand->hasName();
  assert(is_reg);
  printParamLine(inst, num_of_operands, operR, nullptr,
                 curr_operand->getType()->getTypeID(),
                 getMemSize(curr_operand->getType()), curr_operand, is_reg);

  const Function::ArgumentListType &Args(fun->getArgumentList());
  int call_id = 0;
  for (Function::ArgumentListType::const_iterator arg_it = Args.begin(),
                                                  arg_end = Args.end();
       arg_it != arg_end; ++arg_it) {
    char curr_arg_name[256];
    strcpy(curr_arg_name, (char *)arg_it->getName().str().c_str());

    curr_operand = inst->getOperand(call_id);
    is_reg = curr_operand->hasName();
    if (Instruction *I = dyn_cast<Instruction>(curr_operand)) {
      int flag = 0;
      is_reg = getInstId(I, nullptr, operR, flag);
      assert(flag == 0);
      if (curr_operand->getType()->isVectorTy()) {
        printParamLine(inst, call_id + 1, operR, nullptr,
                       curr_operand->getType()->getTypeID(),
                       getMemSize(curr_operand->getType()), nullptr, is_reg);
        printParamLine(inst, FORWARD_LINE, curr_arg_name, nullptr,
                       curr_operand->getType()->getTypeID(),
                       getMemSize(curr_operand->getType()), nullptr, true);
      } else {
        printParamLine(inst, call_id + 1, operR, nullptr,
                       I->getType()->getTypeID(), getMemSize(I->getType()),
                       curr_operand, is_reg);
        printParamLine(inst, FORWARD_LINE, curr_arg_name, nullptr,
                       I->getType()->getTypeID(), getMemSize(I->getType()),
                       curr_operand, true);
      }
    } else {
      if (curr_operand->getType()->isVectorTy()) {
        char operand_id[256];
        strcpy(operand_id, curr_operand->getName().str().c_str());
        printParamLine(inst, call_id + 1, operand_id, nullptr,
                       curr_operand->getType()->getTypeID(),
                       getMemSize(curr_operand->getType()), nullptr, is_reg);
        printParamLine(inst, FORWARD_LINE, curr_arg_name, nullptr,
                       curr_operand->getType()->getTypeID(),
                       getMemSize(curr_operand->getType()), nullptr, true);
      } else if (curr_operand->getType()->isLabelTy()) {
        char label_id[256];
        getBBId(curr_operand, label_id);
        printParamLine(inst, call_id + 1, label_id, nullptr,
                       curr_operand->getType()->getTypeID(),
                       getMemSize(curr_operand->getType()), nullptr, true);
        printParamLine(inst, FORWARD_LINE, curr_arg_name, nullptr,
                       curr_operand->getType()->getTypeID(),
                       getMemSize(curr_operand->getType()), nullptr, true);
      }
      // is function
      else if (curr_operand->getValueID() == 2) {
        char func_id[256];
        strcpy(func_id, curr_operand->getName().str().c_str());
        printParamLine(inst, call_id + 1, func_id, nullptr,
                       curr_operand->getType()->getTypeID(),
                       getMemSize(curr_operand->getType()), nullptr, is_reg);
        printParamLine(inst, FORWARD_LINE, curr_arg_name, nullptr,
                       curr_operand->getType()->getTypeID(),
                       getMemSize(curr_operand->getType()), nullptr, true);
      } else {
        char operand_id[256];
        strcpy(operand_id, curr_operand->getName().str().c_str());
        printParamLine(inst, call_id + 1, operand_id, nullptr,
                       curr_operand->getType()->getTypeID(),
                       getMemSize(curr_operand->getType()), curr_operand,
                       is_reg);
        printParamLine(inst, FORWARD_LINE, curr_arg_name, nullptr,
                       curr_operand->getType()->getTypeID(),
                       getMemSize(curr_operand->getType()), curr_operand, true);
      }
    }
    call_id++;
  }
}

void Tracer::handleNonPhiNonCallInstruction(Instruction *inst, InstEnv* env) {
  char operR[256];
  printFirstLine(inst, env, inst->getOpcode());
  int num_of_operands = inst->getNumOperands();
  if (num_of_operands > 0) {
    for (int i = num_of_operands - 1; i >= 0; i--) {
      Value* curr_operand = inst->getOperand(i);
      bool is_reg = curr_operand->hasName();

      // for instructions using registers
      if (Instruction *I = dyn_cast<Instruction>(curr_operand)) {
        int flag = 0;
        is_reg = getInstId(I, nullptr, operR, flag);
        assert(flag == 0);
        if (curr_operand->getType()->isVectorTy()) {
          printParamLine(inst, i + 1, operR, nullptr,
                         curr_operand->getType()->getTypeID(),
                         getMemSize(curr_operand->getType()), nullptr, is_reg);
        } else {
          printParamLine(inst, i + 1, operR, nullptr, I->getType()->getTypeID(),
                         getMemSize(I->getType()), curr_operand, is_reg);
        }
      } else {
        if (curr_operand->getType()->isVectorTy()) {
          char operand_id[256];
          strcpy(operand_id, curr_operand->getName().str().c_str());
          printParamLine(inst, i + 1, operand_id, nullptr,
                         curr_operand->getType()->getTypeID(),
                         getMemSize(curr_operand->getType()), nullptr, is_reg);
        } else if (curr_operand->getType()->isLabelTy()) {
          char label_id[256];
          getBBId(curr_operand, label_id);
          printParamLine(inst, i + 1, label_id, nullptr,
                         curr_operand->getType()->getTypeID(),
                         getMemSize(curr_operand->getType()), nullptr, true);
        }
        // is function
        else if (curr_operand->getValueID() == 2) {
          char func_id[256];
          strcpy(func_id, curr_operand->getName().str().c_str());
          printParamLine(inst, i + 1, func_id, nullptr,
                         curr_operand->getType()->getTypeID(),
                         getMemSize(curr_operand->getType()), nullptr, is_reg);
        } else {
          char operand_id[256];
          strcpy(operand_id, curr_operand->getName().str().c_str());
          printParamLine(inst, i + 1, operand_id, nullptr,
                         curr_operand->getType()->getTypeID(),
                         getMemSize(curr_operand->getType()), curr_operand,
                         is_reg);
        }
      }
    }
  }
}

void Tracer::handleInstructionResult(Instruction *inst, Instruction *next_inst,
                                     InstEnv *env) {
  bool is_reg = true;
  if (inst->getType()->isVectorTy()) {
    printParamLine(next_inst, RESULT_LINE, env->instid, nullptr,
                   inst->getType()->getTypeID(), getMemSize(inst->getType()),
                   nullptr, is_reg);
  } else if (inst->isTerminator())
    printf("It is terminator...\n");
  else {
    printParamLine(next_inst, RESULT_LINE, env->instid, nullptr,
                   inst->getType()->getTypeID(), getMemSize(inst->getType()),
                   inst, is_reg);
  }
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

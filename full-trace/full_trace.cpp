namespace std {
class type_info;
}
#include <vector>
#include <map>
#include <cmath>
#include "llvm/Pass.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Metadata.h"
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <set>
#include <sstream>
#include "SlotTracker.h"

#define NUM_OF_INTRINSICS 35
#define NUM_OF_LLVM_INTRINSICS 33
#define RESULT_LINE 19134
#define FORWARD_LINE 24601
#define DMA_STORE 98
#define DMA_LOAD 99
using namespace llvm;
using namespace std;

namespace {

  void split(const std::string &s, const char delim, std::set<std::string> &elems) {
      std::istringstream ss(s);
      std::string item;
      while (std::getline(ss, item, delim)) {
          elems.insert(item);
      }
  }


}// end of anonymous namespace
char list_of_intrinsics[NUM_OF_INTRINSICS]
                       [25] = { "llvm.memcpy", // standard C lib
                                "llvm.memmove", "llvm.memset", "llvm.sqrt",
                                "llvm.powi", "llvm.sin", "llvm.cos", "llvm.pow",
                                "llvm.exp", "llvm.exp2", "llvm.log",
                                "llvm.log10", "llvm.log2", "llvm.fma",
                                "llvm.fabs", "llvm.copysign", "llvm.floor",
                                "llvm.ceil", "llvm.trunc", "llvm.rint",
                                "llvm.nearbyint", "llvm.round",
                                "llvm.bswap", // bit manipulation
                                "llvm.ctpop", "llvm.ctlz", "llvm.cttz",
                                "llvm.sadd.with.overflow", // arithmetic with
                                                           // overflow
                                "llvm.uadd.with.overflow",
                                "llvm.ssub.with.overflow",
                                "llvm.usub.with.overflow",
                                "llvm.smul.with.overflow",
                                "llvm.umul.with.overflow",
                                "llvm.fmuladd", // specialised arithmetic
                                "dmaLoad", "dmaStore", };

namespace {

struct fullTrace : public BasicBlockPass {
  static char ID;

  // External trace_logger function
  Value *TL_log0, *TL_log_int, *TL_log_double;

  fullTrace() : BasicBlockPass(ID) {}

  Module *curr_module;
  SlotTracker *st;
  Function *curr_function;

  std::set<std::string> functions;
  std::map<string, string> mangled_to_original_name;

  virtual bool doInitialization(Module &M) {
    // Add external trace_logger function declaratio
    TL_log0 = M.getOrInsertFunction(
        "trace_logger_log0", Type::getVoidTy(M.getContext()),
        Type::getInt64Ty(M.getContext()), Type::getInt8PtrTy((M.getContext())),
        Type::getInt8PtrTy((M.getContext())),
        Type::getInt8PtrTy((M.getContext())), Type::getInt64Ty(M.getContext()),
        NULL);

    TL_log_int = M.getOrInsertFunction(
        "trace_logger_log_int", Type::getVoidTy(M.getContext()),
        Type::getInt64Ty(M.getContext()), Type::getInt64Ty(M.getContext()),
        Type::getInt64Ty(M.getContext()), Type::getInt64Ty(M.getContext()),
        Type::getInt8PtrTy((M.getContext())), Type::getInt64Ty(M.getContext()),
        Type::getInt8PtrTy((M.getContext())), NULL);

    TL_log_double = M.getOrInsertFunction(
        "trace_logger_log_double", Type::getVoidTy(M.getContext()),
        Type::getInt64Ty(M.getContext()), Type::getInt64Ty(M.getContext()),
        Type::getDoubleTy(M.getContext()), Type::getInt64Ty(M.getContext()),
        Type::getInt8PtrTy((M.getContext())), Type::getInt64Ty(M.getContext()),
        Type::getInt8PtrTy((M.getContext())), NULL);

    std::string func_string = getenv("WORKLOAD");
    if (func_string.empty()) {
      std::cerr << "Please set WORKLOAD as an environment variable!\n";
      return false;
    }
    split(func_string, ',', this->functions);

    st = createSlotTracker(&M);
    st->initialize();
    curr_module = &M;
    curr_function = NULL;

    DebugInfoFinder Finder;
    Finder.processModule(M);
    for (auto i = Finder.subprograms().begin(),
              e = Finder.subprograms().end();
         i != e; ++i) {
      DISubprogram S(*i);
      mangled_to_original_name[S.getLinkageName().str()] = S.getName().str();
    }

    return false;
  }

  int trace_or_not(char *func) {
    if (is_tracking_function(func))
      return 1;
    for (int i = 0; i < NUM_OF_INTRINSICS; i++)
      if (strstr(func, list_of_intrinsics[i]) == func) {
        // TODO: Super hacky way of ensuring that dmaLoad and dmaStore always
        // get tracked (by adding them as llvm intrinsics). We should come up
        // with a better way of doing this...
        if (i < NUM_OF_LLVM_INTRINSICS)
          return i + 2;
        else
          return 1;
      }
    return -1;
  }

  bool is_tracking_function(string func) {
    // perform search in log(n) time.
    std::set<std::string>::iterator it = this->functions.find(func);
    if (it != this->functions.end()) {
        return true;
    }
    return false;
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

  Constant *createStringArg(char *string) {
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

  void createCallForParameterLine(BasicBlock::iterator itr, int line,
                                  int datasize, int datatype = 64,
                                  bool is_reg = 0, char *reg_id = NULL,
                                  Value *value = NULL, bool is_phi = 0,
                                  char *prev_bbid = "phi") {
    IRBuilder<> IRB(itr);
    Value *v_line = ConstantInt::get(IRB.getInt64Ty(), line);
    Value *v_size = ConstantInt::get(IRB.getInt64Ty(), datasize);
    Value *v_is_reg = ConstantInt::get(IRB.getInt64Ty(), is_reg);
    Value *v_is_phi = ConstantInt::get(IRB.getInt64Ty(), is_phi);
    Constant *vv_reg_id = createStringArg(reg_id);
    Constant *vv_prev_bbid = createStringArg(prev_bbid);
    ;
    if (value != NULL) {
      if (datatype == llvm::Type::IntegerTyID) {
        Value *v_value = IRB.CreateZExt(value, IRB.getInt64Ty());
        Value *args[] = { v_line,    v_size,   v_value,     v_is_reg,
                          vv_reg_id, v_is_phi, vv_prev_bbid };
        IRB.CreateCall(TL_log_int, args);
      } else if (datatype >= llvm::Type::HalfTyID &&
                 datatype <= llvm::Type::PPC_FP128TyID) {
        Value *v_value = IRB.CreateFPExt(value, IRB.getDoubleTy());
        Value *args[] = { v_line,    v_size,   v_value,     v_is_reg,
                          vv_reg_id, v_is_phi, vv_prev_bbid };
        IRB.CreateCall(TL_log_double, args);
      } else if (datatype == llvm::Type::PointerTyID) {
        Value *v_value = IRB.CreatePtrToInt(value, IRB.getInt64Ty());
        Value *args[] = { v_line,    v_size,   v_value,     v_is_reg,
                          vv_reg_id, v_is_phi, vv_prev_bbid };
        IRB.CreateCall(TL_log_int, args);
      } else
        fprintf(stderr, "normal data else: %d, %s\n", datatype, reg_id);
    } else {
      Value *v_value = ConstantInt::get(IRB.getInt64Ty(), 0);
      Value *args[] = { v_line,    v_size,   v_value,     v_is_reg,
                        vv_reg_id, v_is_phi, vv_prev_bbid };
      IRB.CreateCall(TL_log_int, args);
    }
  }

  // s - function ID or register ID or label name
  // opty - opcode or data type
  void print_line(BasicBlock::iterator itr, int line, int line_number,
                  char *func_or_reg_id, char *bbID, char *instID, int opty,
                  int datasize = 0, Value *value = NULL, bool is_reg = 0,
                  char *prev_bbid = "phi") {

    /*Print instruction line*/
    if (line == 0) {
      IRBuilder<> IRB(itr);
      Value *v_line, *v_opty, *v_value, *v_linenumber;
      v_line = ConstantInt::get(IRB.getInt64Ty(), line);
      v_opty = ConstantInt::get(IRB.getInt64Ty(), opty);
      v_linenumber = ConstantInt::get(IRB.getInt64Ty(), line_number);
      Constant *vv_func_id = createStringArg(func_or_reg_id);
      Constant *vv_bb = createStringArg(bbID);
      Constant *vv_inst = createStringArg(instID);
      IRB.CreateCall5(TL_log0, v_linenumber, vv_func_id, vv_bb, vv_inst,
                      v_opty);
    }
    /*Print parameter/result line*/
    else {
      bool is_phi = 0;
      if (bbID != NULL && strcmp(bbID, "phi") == 0)
        is_phi = 1;
      createCallForParameterLine(itr, line, datasize, opty, is_reg,
                                 func_or_reg_id, value, is_phi, prev_bbid);
    }
  }

  bool getInstId(Instruction *itr, char *bbid, char *instid, int &instc) {
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
      if (bbid != NULL)
        strcpy(instid, bbid);
      strcat(instid, dash);
      strcat(instid, tmp);
      instc++;
      return true;
    }
    return false;
  }

  void getBBId(Value *BB, char *bbid) {
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

  virtual bool runOnBasicBlock(BasicBlock &BB) {
    Function *func = BB.getParent();
    int instc = 0;
    char funcName[256];

    if (curr_function != func) {
      st->purgeFunction();
      st->incorporateFunction(func);
      curr_function = func;
    }
    std::map<string, string>::iterator it =
        mangled_to_original_name.find(curr_function->getName().str());
    if (it != mangled_to_original_name.end()) {
      strcpy(funcName, it->second.c_str());
    } else {
      strcpy(funcName, curr_function->getName().str().c_str());
    }

    if (!is_tracking_function(funcName))
      return false;

    std::cout << "Tracking function: " << funcName << std::endl;
    /*For PHI Nodes*/
    BasicBlock::iterator insertp = BB.getFirstInsertionPt();
    BasicBlock::iterator itr = BB.begin();
    if (dyn_cast<PHINode>(itr)) {
      for (; dyn_cast<PHINode>(itr) != NULL; itr++) {

        Value *curr_operand = NULL;
        bool is_reg = 0;
        int size = 0;
        char bbid[256], instid[256], operR[256];
        int line_number = -1;

        getBBId(&BB, bbid);
        getInstId(itr, bbid, instid, instc);
        int opcode = itr->getOpcode();

        if (MDNode *N = itr->getMetadata("dbg")) {
          DILocation Loc(N); // DILocation is in DebugInfo.h
          line_number = Loc.getLineNumber();
        }
        print_line(insertp, 0, line_number, funcName, bbid, instid, opcode);

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
              is_reg = getInstId(I, NULL, operR, flag);
              assert(flag == 0);
              if (curr_operand->getType()->isVectorTy()) {
                print_line(insertp, i + 1, -1, operR, "phi", NULL,
                           curr_operand->getType()->getTypeID(),
                           getMemSize(curr_operand->getType()), NULL, is_reg,
                           prev_bbid);
              } else {
                print_line(insertp, i + 1, -1, operR, "phi", NULL,
                           I->getType()->getTypeID(), getMemSize(I->getType()),
                           NULL, is_reg, prev_bbid);
              }
            } else if (curr_operand->getType()->isVectorTy()) {
              char operand_id[256];
              strcpy(operand_id, curr_operand->getName().str().c_str());
              print_line(insertp, i + 1, -1, operand_id, "phi", NULL,
                         curr_operand->getType()->getTypeID(),
                         getMemSize(curr_operand->getType()), NULL, is_reg,
                         prev_bbid);
            } else {
              char operand_id[256];
              strcpy(operand_id, curr_operand->getName().str().c_str());
              print_line(insertp, i + 1, -1, operand_id, "phi", NULL,
                         curr_operand->getType()->getTypeID(),
                         getMemSize(curr_operand->getType()), curr_operand,
                         is_reg, prev_bbid);
            }
          }
        }
        /*Print result line*/
        if (!itr->getType()->isVoidTy()) {
          is_reg = 1;
          if (itr->getType()->isVectorTy()) {
            print_line(insertp, RESULT_LINE, -1, instid, NULL, NULL,
                       itr->getType()->getTypeID(), getMemSize(itr->getType()),
                       NULL, is_reg);
          } else if (itr->isTerminator())
            fprintf(stderr, "It is terminator...\n");
          else {
            print_line(insertp, RESULT_LINE, -1, instid, NULL, NULL,
                       itr->getType()->getTypeID(), getMemSize(itr->getType()),
                       itr, is_reg);
          }
        }
      }
    }

    /*For Non-Phi Instructions*/
    BasicBlock::iterator nextitr;
    for (BasicBlock::iterator itr = insertp; itr != BB.end(); itr = nextitr) {
      Value *curr_operand = NULL;
      bool is_reg = 0;
      int size = 0;
      char bbid[256], instid[256], operR[256];
      int line_number = -1;

      nextitr = itr;
      nextitr++;

      // Get static BasicBlock ID: produce bbid
      getBBId(&BB, bbid);
      // Get static instruction ID: produce instid
      getInstId(itr, bbid, instid, instc);

      // Get opcode: produce opcode
      int opcode = itr->getOpcode();

      if (MDNode *N = itr->getMetadata("dbg")) {
        DILocation Loc(N); // DILocation is in DebugInfo.h
        line_number = Loc.getLineNumber();
      }
      int callType = -1;
      if (CallInst *I = dyn_cast<CallInst>(itr)) {
        char callfunc[256];
        Function *fun = I->getCalledFunction();
        if (fun)
          strcpy(callfunc, fun->getName().str().c_str());
        callType = trace_or_not(callfunc);
        if (callType == -1)
          continue;
      }

      int num_of_operands = itr->getNumOperands();
      /*Call Instruction*/
      if (itr->getOpcode() == Instruction::Call && callType == 1) {
        CallInst *CI = dyn_cast<CallInst>(itr);
        Function *fun = CI->getCalledFunction();
        strcpy(operR, (char *)fun->getName().str().c_str());
        if (fun->getName().str().find("dmaLoad") != std::string::npos)
          print_line(itr, 0, line_number, funcName, bbid, instid, DMA_LOAD);
        else if (fun->getName().str().find("dmaStore") != std::string::npos)
          print_line(itr, 0, line_number, funcName, bbid, instid, DMA_STORE);
        else
          print_line(itr, 0, line_number, funcName, bbid, instid, opcode);

        curr_operand = itr->getOperand(num_of_operands - 1);
        is_reg = curr_operand->hasName();
        assert(is_reg);
        print_line(itr, num_of_operands, -1, operR, NULL, NULL,
                   curr_operand->getType()->getTypeID(),
                   getMemSize(curr_operand->getType()), curr_operand, is_reg);

        const Function::ArgumentListType &Args(fun->getArgumentList());
        int num_of_call_operands = CI->getNumArgOperands();
        int call_id = 0;
        for (Function::ArgumentListType::const_iterator arg_it = Args.begin(),
                                                        arg_end = Args.end();
             arg_it != arg_end; ++arg_it) {
          char curr_arg_name[256];
          strcpy(curr_arg_name, (char *)arg_it->getName().str().c_str());

          curr_operand = itr->getOperand(call_id);
          is_reg = curr_operand->hasName();
          if (Instruction *I = dyn_cast<Instruction>(curr_operand)) {
            int flag = 0;
            is_reg = getInstId(I, NULL, operR, flag);
            assert(flag == 0);
            if (curr_operand->getType()->isVectorTy()) {
              print_line(itr, call_id + 1, -1, operR, NULL, NULL,
                         curr_operand->getType()->getTypeID(),
                         getMemSize(curr_operand->getType()), NULL, is_reg);
              print_line(itr, FORWARD_LINE, -1, curr_arg_name, NULL, NULL,
                         curr_operand->getType()->getTypeID(),
                         getMemSize(curr_operand->getType()), NULL, true);
            } else {
              print_line(itr, call_id + 1, -1, operR, NULL, NULL,
                         I->getType()->getTypeID(), getMemSize(I->getType()),
                         curr_operand, is_reg);
              print_line(itr, FORWARD_LINE, -1, curr_arg_name, NULL, NULL,
                         I->getType()->getTypeID(), getMemSize(I->getType()),
                         curr_operand, true);
            }
          } else {
            if (curr_operand->getType()->isVectorTy()) {
              char operand_id[256];
              strcpy(operand_id, curr_operand->getName().str().c_str());
              print_line(itr, call_id + 1, -1, operand_id, NULL, NULL,
                         curr_operand->getType()->getTypeID(),
                         getMemSize(curr_operand->getType()), NULL, is_reg);
              print_line(itr, FORWARD_LINE, -1, curr_arg_name, NULL, NULL,
                         curr_operand->getType()->getTypeID(),
                         getMemSize(curr_operand->getType()), NULL, true);
            } else if (curr_operand->getType()->isLabelTy()) {
              char label_id[256];
              getBBId(curr_operand, label_id);
              print_line(itr, call_id + 1, -1, label_id, NULL, NULL,
                         curr_operand->getType()->getTypeID(),
                         getMemSize(curr_operand->getType()), NULL, true);
              print_line(itr, FORWARD_LINE, -1, curr_arg_name, NULL, NULL,
                         curr_operand->getType()->getTypeID(),
                         getMemSize(curr_operand->getType()), NULL, true);
            }
            // is function
            else if (curr_operand->getValueID() == 2) {
              char func_id[256];
              strcpy(func_id, curr_operand->getName().str().c_str());
              print_line(itr, call_id + 1, -1, func_id, NULL, NULL,
                         curr_operand->getType()->getTypeID(),
                         getMemSize(curr_operand->getType()), NULL, is_reg);
              print_line(itr, FORWARD_LINE, -1, curr_arg_name, NULL, NULL,
                         curr_operand->getType()->getTypeID(),
                         getMemSize(curr_operand->getType()), NULL, true);
            } else {
              char operand_id[256];
              strcpy(operand_id, curr_operand->getName().str().c_str());
              print_line(itr, call_id + 1, -1, operand_id, NULL, NULL,
                         curr_operand->getType()->getTypeID(),
                         getMemSize(curr_operand->getType()), curr_operand,
                         is_reg);
              print_line(itr, FORWARD_LINE, -1, curr_arg_name, NULL, NULL,
                         curr_operand->getType()->getTypeID(),
                         getMemSize(curr_operand->getType()), curr_operand,
                         true);
            }
          }
          call_id++;
        }
      }
      /*Non-Phi, Non-Call instructions*/
      else {
        print_line(itr, 0, line_number, funcName, bbid, instid, opcode);
        if (num_of_operands > 0) {
          for (int i = num_of_operands - 1; i >= 0; i--) {
            curr_operand = itr->getOperand(i);
            is_reg = curr_operand->hasName();
            char arg_label_in_callee[256];

            // for instructions using registers
            if (Instruction *I = dyn_cast<Instruction>(curr_operand)) {
              int flag = 0;
              is_reg = getInstId(I, NULL, operR, flag);
              assert(flag == 0);
              if (curr_operand->getType()->isVectorTy()) {
                print_line(itr, i + 1, -1, operR, NULL, NULL,
                           curr_operand->getType()->getTypeID(),
                           getMemSize(curr_operand->getType()), NULL, is_reg);
              } else {
                print_line(itr, i + 1, -1, operR, NULL, NULL,
                           I->getType()->getTypeID(), getMemSize(I->getType()),
                           curr_operand, is_reg);
              }
            } else {
              if (curr_operand->getType()->isVectorTy()) {
                char operand_id[256];
                strcpy(operand_id, curr_operand->getName().str().c_str());
                print_line(itr, i + 1, -1, operand_id, NULL, NULL,
                           curr_operand->getType()->getTypeID(),
                           getMemSize(curr_operand->getType()), NULL, is_reg);
              } else if (curr_operand->getType()->isLabelTy()) {
                char label_id[256];
                getBBId(curr_operand, label_id);
                print_line(itr, i + 1, -1, label_id, NULL, NULL,
                           curr_operand->getType()->getTypeID(),
                           getMemSize(curr_operand->getType()), NULL, true);
              }
              // is function
              else if (curr_operand->getValueID() == 2) {
                char func_id[256];
                strcpy(func_id, curr_operand->getName().str().c_str());
                print_line(itr, i + 1, -1, func_id, NULL, NULL,
                           curr_operand->getType()->getTypeID(),
                           getMemSize(curr_operand->getType()), NULL, is_reg);
              } else {
                char operand_id[256];
                strcpy(operand_id, curr_operand->getName().str().c_str());
                print_line(itr, i + 1, -1, operand_id, NULL, NULL,
                           curr_operand->getType()->getTypeID(),
                           getMemSize(curr_operand->getType()), curr_operand,
                           is_reg);
              }
            }
          }
        }
      }

      // for call instruction

      // handle function result
      if (!itr->getType()->isVoidTy()) {
        is_reg = 1;
        if (itr->getType()->isVectorTy()) {
          print_line(nextitr, RESULT_LINE, -1, instid, NULL, NULL,
                     itr->getType()->getTypeID(), getMemSize(itr->getType()),
                     NULL, is_reg);
        } else if (itr->isTerminator())
          printf("It is terminator...\n");
        else {
          print_line(nextitr, RESULT_LINE, -1, instid, NULL, NULL,
                     itr->getType()->getTypeID(), getMemSize(itr->getType()),
                     itr, is_reg);
        }
      }
    }
    return false;
  }
  // runBasicBlock
};
// struct

} // namespace

char fullTrace::ID = 0;
static RegisterPass<fullTrace>
X("fulltrace", "Add full Tracing Instrumentation for Aladdin", false, false);

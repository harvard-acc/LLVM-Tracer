/* AST pass to extract labeled statements.
 *
 * Labeled statements are available in the AST but are stripped out in emitted
 * IR, so this cannot be done in the LLVM pass.
 *
 * Label information is stored in a temporary file called labelmap, where each
 * line is of the following form:
 *    function/label_name line_number
 *
 * For example:
 *    foo/bar 17
 *    SomeMangledFunctionName/bar 42
 *
 * Usage:
 *   ./get-labeled-stmts [SOURCE_FILES...] -- -I${LLVM_HOME}/lib/clang/3.4/include
 */

#include <cstdio>
#include <fstream>
#include <iostream>
#include <map>
#include <stack>
#include <sys/stat.h>

#include "clang/AST/AST.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/Attr.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Basic/AttrKinds.h"
#include "clang/Driver/Options.h"
#include "clang/Frontend/ASTConsumers.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/Debug.h"

using namespace clang;
using namespace clang::driver;
using namespace clang::tooling;
using namespace llvm;

#define DEBUG_TYPE "get-labeled-stmts"

// The option categories, to group options in the man page. It is useless
// here, since we don't have any new options in this tool.
// Declaring here because CommonOptionsParser requires this one in LLVM 3.5.
// http://llvm.org/docs/CommandLine.html#grouping-options-into-categories
cl::OptionCategory GetLabelStmtsCat("GetLabelStmts options");
static cl::opt<std::string> OutputFileName("output",
                                           cl::desc("Specify output filename"),
                                           cl::value_desc("filename"),
                                           cl::init("labelmap"));

// A class containing all the labels and caller information for a function.
class FunctionInfo {
 public:
  typedef std::map<std::string, unsigned> labelmap_t;
  typedef std::set<std::string> caller_set_t;
  FunctionInfo() : inlined(false) {}

  unsigned& operator[](const std::string& label) {
    if (labelToLineNum.find(label) == labelToLineNum.end())
      labelToLineNum[label] = 0;
    return labelToLineNum[label];
  }

  labelmap_t::const_iterator label_begin() const {
    return labelToLineNum.cbegin();
  }

  labelmap_t::const_iterator label_end() const {
    return labelToLineNum.cend();
  }

  caller_set_t::const_iterator caller_begin() const {
    return callers.cbegin();
  }

  caller_set_t::const_iterator caller_end() const {
    return callers.cend();
  }

  void add_caller(std::string function) {
    callers.insert(function);
  }

  bool is_inlined() const { return inlined; }
  void set_inlined(bool _inlined) { inlined = _inlined; }

 private:
  labelmap_t labelToLineNum;
  caller_set_t callers;
  bool inlined;
};

// Maps pairs of (func_name, label_name) to line numbers.
static std::map<std::string, FunctionInfo> labelMap;
static std::string outputFileName;

class LabeledStmtVisitor : public RecursiveASTVisitor<LabeledStmtVisitor> {
 private:
  ASTContext* astContext;
  SourceManager* srcManager;

 public:
  explicit LabeledStmtVisitor(CompilerInstance* CI)
      : astContext(&(CI->getASTContext())),
        srcManager(&(CI->getSourceManager())) {}


  // Add a (function,label) -> line number mapping.
  void addToLabelMap(LabelStmt* labelStmt, Stmt* subStmt,
                     const FunctionDecl* func) const {
    SourceLocation loc = subStmt->getLocStart();
    unsigned line = srcManager->getExpansionLineNumber(loc);
    const std::string& labelName = labelStmt->getName();
    const std::string& funcName = func->getQualifiedNameAsString();
    labelMap[funcName][labelName] = line;
  }

  // Handle a labeled statement, if it is one.
  //
  // If the statement is a labeled statement, then find its first non-labeled
  // substatement, and associate this label with that substatement in the label
  // map. This ensures that any label will be associated with the next actual
  // line of code executed, even if that next statement is not on the same line
  // or if there are other labels in between.
  void handleIfLabelStmt(Stmt* st, const FunctionDecl* func = nullptr) const {
    if (!func || !st)
      return;
    LabelStmt* labelStmt = dyn_cast<LabelStmt>(st);
    if (labelStmt) {
      Stmt* subStmt = labelStmt->getSubStmt();
      while (dyn_cast<LabelStmt>(subStmt)) {
        subStmt = dyn_cast<LabelStmt>(subStmt)->getSubStmt();
      }
      addToLabelMap(labelStmt, subStmt, func);
    }
  }

  // Handle a call expression, if it is one.
  //
  // If the statement is a Call expression, add the calling function to the set
  // of caller functions in the callee function's FunctionInfo object. This
  // will be used at the end to resolve the true location of inlined functions.
  void handleIfCallExpr(Stmt* st, const FunctionDecl* caller_func) const {
    if (!caller_func || !st || caller_func->isMain())
      return;
    CallExpr* callExpr = dyn_cast<CallExpr>(st);
    if (callExpr) {
      const FunctionDecl* callee = callExpr->getDirectCallee();
      if (callee) {
        labelMap[callee->getQualifiedNameAsString()].add_caller(
            caller_func->getQualifiedNameAsString());
      }
    }
  }

  void VisitAllChildStmtsOf(Stmt* stmt, const FunctionDecl* func) const {
    if (!stmt)
      return;
    std::stack<Stmt*> stack;
    for (Stmt::child_iterator it = stmt->child_begin();
         it != stmt->child_end(); ++it) {
      Stmt* childStmt = *it;
      stack.push(childStmt);
      handleIfLabelStmt(childStmt, func);
      handleIfCallExpr(childStmt, func);
    }
    while (!stack.empty()) {
      Stmt* next = stack.top();
      VisitAllChildStmtsOf(next, func);
      stack.pop();
    }
  }

  // For each function, recursively find every child label statement.
  bool VisitFunctionDecl(const FunctionDecl *func) const {
    // Only support free functions, not class member functions or templates.
    if (isa<CXXMethodDecl>(func))
      return true;

    if (func->hasBody()) {
      const std::string& funcName = func->getQualifiedNameAsString();
      if (labelMap.find(funcName) == labelMap.end())
        labelMap[funcName] = FunctionInfo();

      for (auto it = func->attr_begin(); it != func->attr_end(); ++it) {
        if ((*it)->getKind() == clang::attr::AlwaysInline)
          labelMap[funcName].set_inlined(true);
      }
      Stmt* body = func->getBody(func);
      VisitAllChildStmtsOf(body, func);
    }
    return true;
  }
};

class LabeledStmtASTConsumer : public ASTConsumer {
 public:
  explicit LabeledStmtASTConsumer(CompilerInstance* CI)
      : visitor(new LabeledStmtVisitor(CI)) {}

  virtual void HandleTranslationUnit(ASTContext& Context) {
    visitor->TraverseDecl(Context.getTranslationUnitDecl());
  }

 private:
  LabeledStmtVisitor* visitor;
};

class LabeledStmtFrontendAction : public ASTFrontendAction {
 public:
   virtual std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
                                                          StringRef file) {
     return std::unique_ptr<ASTConsumer>(new LabeledStmtASTConsumer(&CI));
   }

  // Write a label to line number mapping to the labelmap file.
  //
  // Example output:
  //    function/label 20
  void writeLabelMapLine(std::ofstream &ofs, const std::string &function,
                         const std::string &label, unsigned line_number) {
    ofs << function << "/" << label << " " << line_number << "\n";
  }

  // Write an inlined label to line number mapping.
  //
  // Example output:
  //    inlined_function/label 20 inline caller_1 caller_2...
  //
  // where inlined_function is the name of the original function that
  // contains the label, and caller_1, caller_2, etc. are all the NON-INLINED
  // functions in which inlined_function is cloned into.
  void writeLabelMapLineWithInline(std::ofstream &ofs,
                                   const std::string &function,
                                   const std::string &label,
                                   unsigned line_number,
                                   std::list<std::string> callers) {
    ofs << function << "/" << label << " " << line_number;
    if (callers.size() == 0) {
      ofs << "\n";
      return;
    }
    ofs << " inline";
    for (auto caller : callers)
      ofs << " " << caller;
    ofs << "\n";
  }

  // Return a list of all non-inlined functions that call leaf_function.
  void findAllNoninlinedCallers(const std::string &leaf_function,
                                std::list<std::string> &callers) {
    const FunctionInfo& leaf = labelMap[leaf_function];
    for (auto it = leaf.caller_begin(); it != leaf.caller_end(); ++it) {
      const std::string& caller_name = *it;
      const FunctionInfo& caller_info = labelMap[caller_name];
      // If the caller function is not inlined, then it is the last function
      // along this call stack that could potentially contain this label.
      // Therefore, we end the recursive search here. Otherwise, we keep
      // searching recursively.
      if (!caller_info.is_inlined())
        callers.push_back(caller_name);
      else
        findAllNoninlinedCallers(caller_name, callers);
    }
  }

  // Dump the contents of labelMap to the output file.
  //
  // Since this is called per source file, labelMap must be cleared afterwards
  // to prevent duplicating output.
  virtual void EndSourceFileAction() {
    std::ofstream ofs;
    ofs.open(outputFileName, std::ofstream::out | std::ofstream::app);
    for (auto func_it = labelMap.begin(); func_it != labelMap.end(); ++func_it) {
      const std::string& funcName = func_it->first;
      const FunctionInfo& funcInfo = func_it->second;

      if (funcInfo.is_inlined()) {
        std::list<std::string> callers;
        findAllNoninlinedCallers(funcName, callers);
        for (auto label_it = funcInfo.label_begin();
             label_it != funcInfo.label_end(); ++label_it) {
          writeLabelMapLineWithInline(ofs, funcName, label_it->first,
                                      label_it->second, callers);
        }
      } else {
        for (auto label_it = funcInfo.label_begin();
             label_it != funcInfo.label_end(); ++label_it) {
          writeLabelMapLine(ofs, funcName, label_it->first, label_it->second);
        }
      }
    }
    ofs.close();
    labelMap.clear();
  }
};

// Check if the output file already exists and delete it if it does.
static void cleanup() {
  struct stat buffer;
  if (stat(outputFileName.c_str(), &buffer) == 0) {
    std::remove(outputFileName.c_str());
  }
}

int main(int argc, const char** argv) {
  CommonOptionsParser op(argc, argv, GetLabelStmtsCat);
  ClangTool Tool(op.getCompilations(), op.getSourcePathList());
  outputFileName = OutputFileName.getValue();
  cleanup();

  std::unique_ptr<FrontendActionFactory>
        actionfactory(newFrontendActionFactory<LabeledStmtFrontendAction>());
  int result = Tool.run(actionfactory.get());
  return result;
}

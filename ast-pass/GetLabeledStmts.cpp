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
#include <map>
#include <stack>
#include <sys/stat.h>

#include "clang/AST/AST.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Driver/Options.h"
#include "clang/Frontend/ASTConsumers.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"

using namespace clang;
using namespace clang::driver;
using namespace clang::tooling;
using namespace llvm;

// The option categories, to group options in the man page. It is useless
// here, since we don't have any new options in this tool.
// Declaring here because CommonOptionsParser requires this one in LLVM 3.5.
// http://llvm.org/docs/CommandLine.html#grouping-options-into-categories
cl::OptionCategory GetLabelStmtsCat("GetLabelStmts options");

// Maps pairs of (func_name, label_name) to line numbers.
static std::map<std::pair<std::string, std::string>, unsigned> labelMap;
static const std::string outputFileName = "labelmap";

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
    std::string labelName(labelStmt->getName());
    std::string funcName = func->getName().str();
    auto key = std::make_pair(funcName, labelName);
    labelMap.insert(std::make_pair(key, line));
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

  void VisitAllChildStmtsOf(Stmt* stmt, const FunctionDecl* func) const {
    if (!stmt)
      return;
    std::stack<Stmt*> stack;
    for (Stmt::child_iterator it = stmt->child_begin();
         it != stmt->child_end(); ++it) {
      Stmt* childStmt = *it;
      stack.push(childStmt);
      handleIfLabelStmt(childStmt, func);
    }
    while (!stack.empty()) {
      Stmt* next = stack.top();
      VisitAllChildStmtsOf(next, func);
      stack.pop();
    }
  }

  // For each function, recursively find every child label statement.
  virtual bool VisitFunctionDecl(const FunctionDecl* func) const {
    if (func->hasBody()) {
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
  virtual ASTConsumer* CreateASTConsumer(CompilerInstance& CI, StringRef file) {
    return new LabeledStmtASTConsumer(&CI);
  }

  // Dump the contents of labelMap to the output file.
  //
  // Since this is called per source file, labelMap must be cleared afterwards
  // to prevent duplicating output.
  virtual void EndSourceFileAction() {
    std::ofstream ofs;
    ofs.open(outputFileName, std::ofstream::out | std::ofstream::app);
    for (auto it = labelMap.begin(); it != labelMap.end(); ++it) {
      auto pair = it->first;
      unsigned line = it->second;
      ofs << pair.first << "/" << pair.second << " " << line << "\n";
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
#if (LLVM_VERSION == 34)
  CommonOptionsParser op(argc, argv);
#elif (LLVM_VERSION == 35)
  CommonOptionsParser op(argc, argv, GetLabelStmtsCat);
#endif

  ClangTool Tool(op.getCompilations(), op.getSourcePathList());
  cleanup();

  // In llvm 3.4, newFrontendActionFactory returns raw pointers.
  // In llvm 3.5, it returns unique_ptr<>
  // Use unique_ptr to keep pointers, therefore being compatible with
  // LLVM 3.4/3.5
  std::unique_ptr<FrontendActionFactory>
        actionfactory(newFrontendActionFactory<LabeledStmtFrontendAction>());
  int result = Tool.run(actionfactory.get());
  return result;
}

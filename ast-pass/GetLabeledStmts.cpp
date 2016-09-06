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

  // If the statement is a labeled statement, add it to labelMap.
  void checkIfLabelStmt(Stmt* st, const FunctionDecl* func = nullptr) const {
    if (!func)
      return;
    LabelStmt* labelStmt = dyn_cast<LabelStmt>(st);
    if (labelStmt) {
      SourceLocation loc = labelStmt->getLocStart();
      unsigned line = srcManager->getExpansionLineNumber(loc);
      std::string labelName(labelStmt->getName());
      std::string funcName = func->getName().str();
      auto key = std::make_pair(labelName, funcName);
      labelMap.insert(std::make_pair(key, line));
    }
  }

  virtual bool VisitFunctionDecl(const FunctionDecl* func) const {
    if (func->hasBody()) {
      Stmt* body = func->getBody(func);
      for (Stmt::child_iterator it = body->child_begin();
           it != body->child_end(); ++it) {
        Stmt* childStmt = *it;
        checkIfLabelStmt(childStmt, func);
      }
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
  CommonOptionsParser op(argc, argv);
  ClangTool Tool(op.getCompilations(), op.getSourcePathList());
  cleanup();
  int result = Tool.run(newFrontendActionFactory<LabeledStmtFrontendAction>());
  return result;
}

#pragma once

#include "giga_drill/mugann/GlobalIndex.h"

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Tooling/Tooling.h"

#include <memory>
#include <set>
#include <vector>

namespace giga_drill {

// AST visitor that performs shadow lookups at ADL call sites and CTAD usages.
class AnalyzerVisitor : public clang::RecursiveASTVisitor<AnalyzerVisitor> {
public:
  AnalyzerVisitor(const GlobalIndex &index, clang::SourceManager &sm,
                  std::vector<Diagnostic> &diagnostics);

  bool VisitCallExpr(clang::CallExpr *expr);
  bool VisitVarDecl(clang::VarDecl *decl);

private:
  const GlobalIndex &index_;
  clang::SourceManager &sm_;
  std::vector<Diagnostic> &diagnostics_;

  // Set of header paths included in the current TU (populated lazily).
  mutable bool includedFilesPopulated_ = false;
  mutable std::set<std::string> includedFiles_;

  void populateIncludedFiles() const;
  bool isFileIncluded(const std::string &path) const;
  std::string formatLocation(clang::SourceLocation loc) const;
  std::string getFilePath(clang::SourceLocation loc) const;
};

// ASTConsumer that drives the AnalyzerVisitor.
class AnalyzerConsumer : public clang::ASTConsumer {
public:
  AnalyzerConsumer(const GlobalIndex &index, clang::SourceManager &sm,
                   std::vector<Diagnostic> &diagnostics);
  void HandleTranslationUnit(clang::ASTContext &context) override;

private:
  AnalyzerVisitor visitor_;
};

// FrontendAction that creates an AnalyzerConsumer.
class AnalyzerAction : public clang::ASTFrontendAction {
public:
  AnalyzerAction(const GlobalIndex &index, std::vector<Diagnostic> &diags);
  std::unique_ptr<clang::ASTConsumer>
  CreateASTConsumer(clang::CompilerInstance &ci, llvm::StringRef file) override;

private:
  const GlobalIndex &index_;
  std::vector<Diagnostic> &diagnostics_;
};

// Factory for creating AnalyzerActions, for use with ClangTool.
class AnalyzerActionFactory : public clang::tooling::FrontendActionFactory {
public:
  AnalyzerActionFactory(const GlobalIndex &index,
                        std::vector<Diagnostic> &diags);
  std::unique_ptr<clang::FrontendAction> create() override;

private:
  const GlobalIndex &index_;
  std::vector<Diagnostic> &diagnostics_;
};

// Analyze coverage-relevant properties across classes in the index.
// Emits diagnostics for GVA linkage mismatches, discardable ODR, etc.
void analyzeCoverageProperties(const GlobalIndex &index,
                               std::vector<Diagnostic> &diagnostics);

// Run the full two-phase analysis: index all sources, then analyze for
// fragile ADL/CTAD resolution. When enableCoverageDiag is true, also
// runs coverage property analysis between phases.
std::vector<Diagnostic>
runAnalysis(const clang::tooling::CompilationDatabase &compDb,
            const std::vector<std::string> &sourceFiles,
            bool enableCoverageDiag = false);

} // namespace giga_drill

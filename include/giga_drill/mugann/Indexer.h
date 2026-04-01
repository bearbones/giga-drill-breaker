#pragma once

#include "giga_drill/mugann/GlobalIndex.h"

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Tooling/Tooling.h"

#include <memory>

namespace giga_drill {

// AST visitor that collects function overloads and deduction guides
// into a GlobalIndex.
class IndexerVisitor : public clang::RecursiveASTVisitor<IndexerVisitor> {
public:
  IndexerVisitor(GlobalIndex &index, clang::SourceManager &sm);

  bool VisitFunctionDecl(clang::FunctionDecl *decl);
  bool VisitCXXDeductionGuideDecl(clang::CXXDeductionGuideDecl *decl);

private:
  GlobalIndex &index_;
  clang::SourceManager &sm_;

  std::string getFilePath(clang::SourceLocation loc) const;
};

// ASTConsumer that drives the IndexerVisitor.
class IndexerConsumer : public clang::ASTConsumer {
public:
  IndexerConsumer(GlobalIndex &index, clang::SourceManager &sm);
  void HandleTranslationUnit(clang::ASTContext &context) override;

private:
  IndexerVisitor visitor_;
};

// FrontendAction that creates an IndexerConsumer.
class IndexerAction : public clang::ASTFrontendAction {
public:
  explicit IndexerAction(GlobalIndex &index);
  std::unique_ptr<clang::ASTConsumer>
  CreateASTConsumer(clang::CompilerInstance &ci, llvm::StringRef file) override;

private:
  GlobalIndex &index_;
};

// Factory for creating IndexerActions, for use with ClangTool.
class IndexerActionFactory : public clang::tooling::FrontendActionFactory {
public:
  explicit IndexerActionFactory(GlobalIndex &index);
  std::unique_ptr<clang::FrontendAction> create() override;

private:
  GlobalIndex &index_;
};

} // namespace giga_drill

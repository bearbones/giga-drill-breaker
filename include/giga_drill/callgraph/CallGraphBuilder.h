// Copyright (c) 2026 The giga-drill-breaker Authors
// Original author: Alex Mason
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "giga_drill/callgraph/CallGraph.h"

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Tooling/Tooling.h"

#include <memory>
#include <set>
#include <string>
#include <vector>

namespace giga_drill {

// Phase 1 visitor: indexes declarations, class hierarchy, function returns.
class CallGraphIndexerVisitor
    : public clang::RecursiveASTVisitor<CallGraphIndexerVisitor> {
public:
  CallGraphIndexerVisitor(CallGraph &graph, clang::SourceManager &sm);

  void setASTContext(clang::ASTContext *ctx) { ctx_ = ctx; }

  bool VisitFunctionDecl(clang::FunctionDecl *decl);
  bool VisitCXXRecordDecl(clang::CXXRecordDecl *decl);
  bool TraverseFunctionDecl(clang::FunctionDecl *decl);
  bool TraverseCXXMethodDecl(clang::CXXMethodDecl *decl);
  bool TraverseCXXConstructorDecl(clang::CXXConstructorDecl *decl);
  bool TraverseCXXDestructorDecl(clang::CXXDestructorDecl *decl);
  bool VisitReturnStmt(clang::ReturnStmt *stmt);

private:
  CallGraph &graph_;
  clang::SourceManager &sm_;
  clang::ASTContext *ctx_ = nullptr;
  std::vector<clang::FunctionDecl *> funcStack_;

  std::string getFilePath(clang::SourceLocation loc) const;
  std::string formatLocation(clang::SourceLocation loc) const;
  std::string getCurrentFunction() const;
  void computeEffectiveImpls(const clang::CXXRecordDecl *cls);
};

// Phase 2 visitor: builds call edges.
class CallGraphEdgeVisitor
    : public clang::RecursiveASTVisitor<CallGraphEdgeVisitor> {
public:
  CallGraphEdgeVisitor(CallGraph &graph, clang::SourceManager &sm);

  void setASTContext(clang::ASTContext *ctx) { ctx_ = ctx; }

  bool TraverseFunctionDecl(clang::FunctionDecl *decl);
  bool TraverseCXXMethodDecl(clang::CXXMethodDecl *decl);
  bool TraverseCXXConstructorDecl(clang::CXXConstructorDecl *decl);
  bool TraverseCXXDestructorDecl(clang::CXXDestructorDecl *decl);

  bool VisitCallExpr(clang::CallExpr *expr);
  bool VisitCXXConstructExpr(clang::CXXConstructExpr *expr);
  bool VisitVarDecl(clang::VarDecl *decl);
  bool VisitDeclRefExpr(clang::DeclRefExpr *expr);

private:
  CallGraph &graph_;
  clang::SourceManager &sm_;
  clang::ASTContext *ctx_ = nullptr;
  std::vector<clang::FunctionDecl *> funcStack_;

  // Track DeclRefExprs that are direct callees or function-pointer arguments,
  // so VisitDeclRefExpr can add Plausible edges only for uncovered refs.
  std::set<const clang::DeclRefExpr *> handledRefs_;

  // Track vars assigned from functions that return function pointers.
  std::map<const clang::VarDecl *, std::set<std::string>> varFuncSources_;

  std::string getFilePath(clang::SourceLocation loc) const;
  std::string formatLocation(clang::SourceLocation loc) const;
  std::string getCurrentFunction() const;
  bool isInUserCode(clang::SourceLocation loc) const;

  void handleVirtualDispatch(const std::string &caller,
                             clang::CXXMethodDecl *method,
                             clang::SourceLocation loc);
  void addConcreteTypeEdges(const std::string &caller,
                            const clang::CXXRecordDecl *cls,
                            clang::SourceLocation loc);
};

// Consumer that runs both phases per TU (for single-TU tests).
class CallGraphBuilderConsumer : public clang::ASTConsumer {
public:
  CallGraphBuilderConsumer(CallGraph &graph, clang::SourceManager &sm);
  void HandleTranslationUnit(clang::ASTContext &context) override;

private:
  CallGraphIndexerVisitor indexer_;
  CallGraphEdgeVisitor edgeBuilder_;
};

// FrontendAction for combined builder.
class CallGraphBuilderAction : public clang::ASTFrontendAction {
public:
  explicit CallGraphBuilderAction(CallGraph &graph);
  std::unique_ptr<clang::ASTConsumer>
  CreateASTConsumer(clang::CompilerInstance &ci, llvm::StringRef file) override;

private:
  CallGraph &graph_;
};

// Factory for single-TU usage and runToolOnCodeWithArgs.
class CallGraphBuilderFactory : public clang::tooling::FrontendActionFactory {
public:
  explicit CallGraphBuilderFactory(CallGraph &graph);
  std::unique_ptr<clang::FrontendAction> create() override;

private:
  CallGraph &graph_;
};

// Build a call graph from a compilation database (multi-TU, two-pass).
CallGraph buildCallGraph(const clang::tooling::CompilationDatabase &compDb,
                         const std::vector<std::string> &files);

} // namespace giga_drill

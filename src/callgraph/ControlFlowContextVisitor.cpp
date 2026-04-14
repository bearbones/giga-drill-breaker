// Copyright (c) 2026 The giga-drill-breaker Authors
// Original author: Alex Mason
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "giga_drill/callgraph/ControlFlowIndex.h"
#include "giga_drill/callgraph/CallGraph.h"
#include "giga_drill/compat/ToolAdjusters.h"

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/StmtCXX.h"
#include "clang/Basic/ExceptionSpecificationType.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Lex/Lexer.h"
#include "clang/Tooling/CompilationDatabase.h"
#include "clang/Tooling/Tooling.h"

#include <set>
#include <string>
#include <vector>

namespace giga_drill {

// ============================================================================
// Helpers (same pattern as CallGraphBuilder.cpp)
// ============================================================================

static std::string formatLocationHelper(clang::SourceManager &sm,
                                        clang::SourceLocation loc) {
  auto sLoc = sm.getSpellingLoc(loc);
  auto file = sm.getFilename(sLoc);
  unsigned line = sm.getSpellingLineNumber(sLoc);
  unsigned col = sm.getSpellingColumnNumber(sLoc);
  return std::string(file) + ":" + std::to_string(line) + ":" +
         std::to_string(col);
}

// ============================================================================
// ControlFlowContextVisitor (Phase 3)
// ============================================================================

class ControlFlowContextVisitor
    : public clang::RecursiveASTVisitor<ControlFlowContextVisitor> {
public:
  ControlFlowContextVisitor(ControlFlowIndex &index, const CallGraph &graph,
                            clang::SourceManager &sm)
      : index_(index), graph_(graph), sm_(sm) {}

  void setASTContext(clang::ASTContext *ctx) { ctx_ = ctx; }

  // -- Function traversal (push/pop funcStack_) ----------------------------

  bool TraverseFunctionDecl(clang::FunctionDecl *decl) {
    funcStack_.push_back(decl);
    bool result = RecursiveASTVisitor::TraverseFunctionDecl(decl);
    funcStack_.pop_back();
    return result;
  }

  bool TraverseCXXMethodDecl(clang::CXXMethodDecl *decl) {
    funcStack_.push_back(decl);
    bool result = RecursiveASTVisitor::TraverseCXXMethodDecl(decl);
    funcStack_.pop_back();
    return result;
  }

  bool TraverseCXXConstructorDecl(clang::CXXConstructorDecl *decl) {
    funcStack_.push_back(decl);
    bool result = RecursiveASTVisitor::TraverseCXXConstructorDecl(decl);
    funcStack_.pop_back();
    return result;
  }

  bool TraverseCXXDestructorDecl(clang::CXXDestructorDecl *decl) {
    funcStack_.push_back(decl);
    bool result = RecursiveASTVisitor::TraverseCXXDestructorDecl(decl);
    funcStack_.pop_back();
    return result;
  }

  // -- Try/catch traversal (push/pop tryScopeStack_) -----------------------

  bool TraverseCXXTryStmt(clang::CXXTryStmt *stmt) {
    TryScopeEntry entry;
    entry.tryLocation = formatLocation(stmt->getTryLoc());
    entry.enclosingFunction = getCurrentFunction();
    entry.depth = tryScopeStack_.size();

    for (unsigned i = 0; i < stmt->getNumHandlers(); ++i) {
      entry.handlers.push_back(
          analyzeCatchClause(stmt->getHandler(i)));
    }

    tryScopeStack_.push_back(std::move(entry));

    // Traverse the try body — calls here see the enclosing try scope.
    TraverseStmt(stmt->getTryBlock());

    // Traverse each catch handler body.
    for (unsigned i = 0; i < stmt->getNumHandlers(); ++i) {
      insideCatchBlock_ = true;
      TraverseStmt(stmt->getHandler(i)->getHandlerBlock());
      insideCatchBlock_ = false;
    }

    tryScopeStack_.pop_back();
    return true; // Skip base traversal — we manually traversed children.
  }

  // -- If-statement traversal (push/pop guardStack_) -----------------------

  bool TraverseIfStmt(clang::IfStmt *stmt) {
    GuardEntry guard;
    guard.conditionText = getSourceText(stmt->getCond());
    guard.location = formatLocation(stmt->getIfLoc());

    // Traverse the then-branch with guard context.
    guard.inTrueBranch = true;
    guardStack_.push_back(guard);
    if (stmt->getThen())
      TraverseStmt(stmt->getThen());
    guardStack_.pop_back();

    // Traverse the else-branch with negated guard context.
    if (stmt->getElse()) {
      guard.inTrueBranch = false;
      guardStack_.push_back(guard);
      TraverseStmt(stmt->getElse());
      guardStack_.pop_back();
    }

    return true; // Skip base — manually traversed.
  }

  // -- Visit call sites and snapshot context -------------------------------

  bool VisitCallExpr(clang::CallExpr *expr) {
    std::string caller = getCurrentFunction();
    if (caller.empty())
      return true;

    if (!isInUserCode(expr->getBeginLoc()))
      return true;

    std::string calleeName;
    if (auto *callee = expr->getDirectCallee()) {
      calleeName = callee->getQualifiedNameAsString();
    } else {
      // Indirect call — record with placeholder name.
      calleeName = "<indirect>";
    }

    // Check if this is an assertion macro (assert, DCHECK, etc.).
    if (isAssertionCall(calleeName)) {
      // Record the assertion as a guard for subsequent calls in scope.
      // We handle this by checking the callee name — the assertion's
      // condition is the first argument.
      if (expr->getNumArgs() > 0) {
        GuardEntry guard;
        guard.conditionText = getSourceText(expr->getArg(0));
        guard.location = formatLocation(expr->getBeginLoc());
        guard.inTrueBranch = true;
        guard.isAssertion = true;
        // Assertions don't create a scope — they guard everything after
        // them in the current block. We record them as guards at this
        // call site for context, but don't push to the stack.
        // For now, just add the assertion context to subsequent calls
        // by storing it as a "point guard".
        assertionGuards_.push_back(guard);
      }
      return true;
    }

    CallSiteContext ctx = buildContext(caller, calleeName,
                                       expr->getBeginLoc());
    index_.addCallSiteContext(std::move(ctx));
    return true;
  }

  bool VisitCXXConstructExpr(clang::CXXConstructExpr *expr) {
    std::string caller = getCurrentFunction();
    if (caller.empty())
      return true;

    if (!isInUserCode(expr->getBeginLoc()))
      return true;

    auto *ctor = expr->getConstructor();
    if (!ctor || ctor->isImplicit())
      return true;

    std::string calleeName = ctor->getQualifiedNameAsString();
    CallSiteContext ctx = buildContext(caller, calleeName,
                                       expr->getBeginLoc());
    index_.addCallSiteContext(std::move(ctx));
    return true;
  }

private:
  ControlFlowIndex &index_;
  const CallGraph &graph_;
  clang::SourceManager &sm_;
  clang::ASTContext *ctx_ = nullptr;

  std::vector<clang::FunctionDecl *> funcStack_;

  struct TryScopeEntry {
    std::string tryLocation;
    std::string enclosingFunction;
    std::vector<CatchHandlerInfo> handlers;
    unsigned depth = 0;
  };
  std::vector<TryScopeEntry> tryScopeStack_;

  struct GuardEntry {
    std::string conditionText;
    std::string location;
    bool inTrueBranch = true;
    bool isAssertion = false;
  };
  std::vector<GuardEntry> guardStack_;
  std::vector<GuardEntry> assertionGuards_;

  bool insideCatchBlock_ = false;

  std::string getCurrentFunction() const {
    if (funcStack_.empty())
      return "";
    return funcStack_.back()->getQualifiedNameAsString();
  }

  std::string formatLocation(clang::SourceLocation loc) const {
    return formatLocationHelper(sm_, loc);
  }

  bool isInUserCode(clang::SourceLocation loc) const {
    if (loc.isInvalid())
      return false;
    return !sm_.isInSystemHeader(sm_.getSpellingLoc(loc));
  }

  CatchHandlerInfo analyzeCatchClause(clang::CXXCatchStmt *catchStmt) {
    CatchHandlerInfo info;
    auto caughtType = catchStmt->getCaughtType();
    if (caughtType.isNull()) {
      info.isCatchAll = true;
    } else {
      info.caughtType = caughtType.getAsString();
    }
    info.location = formatLocation(catchStmt->getCatchLoc());
    return info;
  }

  NoexceptSpec extractNoexceptSpec(const clang::FunctionDecl *decl) const {
    auto *proto = decl->getType()->getAs<clang::FunctionProtoType>();
    if (!proto)
      return NoexceptSpec::None;

    switch (proto->getExceptionSpecType()) {
    case clang::EST_BasicNoexcept:
    case clang::EST_NoexceptTrue:
      return NoexceptSpec::Noexcept;
    case clang::EST_NoexceptFalse:
      return NoexceptSpec::NoexceptFalse;
    case clang::EST_DynamicNone:
      return NoexceptSpec::ThrowNone;
    case clang::EST_None:
    case clang::EST_Dynamic:
      return NoexceptSpec::None;
    default:
      return NoexceptSpec::Unknown;
    }
  }

  std::string getSourceText(const clang::Stmt *stmt) const {
    if (!stmt || !ctx_)
      return "";
    auto range = clang::CharSourceRange::getTokenRange(stmt->getSourceRange());
    auto text = clang::Lexer::getSourceText(range, sm_, ctx_->getLangOpts());
    // Truncate very long conditions for readability.
    std::string result(text);
    if (result.size() > 200)
      result = result.substr(0, 197) + "...";
    return result;
  }

  bool isAssertionCall(const std::string &name) const {
    // Match common assertion functions/macros.
    return name == "__assert_fail" || name == "__assert_rtn" ||
           name == "__assert" || name.find("DCHECK") != std::string::npos ||
           name.find("CHECK") != std::string::npos ||
           name.find("ASSERT") != std::string::npos;
  }

  CallSiteContext buildContext(const std::string &caller,
                               const std::string &calleeName,
                               clang::SourceLocation callLoc) const {
    CallSiteContext ctx;
    ctx.callerName = caller;
    ctx.calleeName = calleeName;
    ctx.callSite = formatLocation(callLoc);
    ctx.insideCatchBlock = insideCatchBlock_;

    // Snapshot try/catch scopes (innermost first = reverse of stack).
    for (auto it = tryScopeStack_.rbegin(); it != tryScopeStack_.rend(); ++it) {
      TryCatchScope scope;
      scope.tryLocation = it->tryLocation;
      scope.enclosingFunction = it->enclosingFunction;
      scope.handlers = it->handlers;
      scope.nestingDepth = it->depth;
      ctx.enclosingTryCatches.push_back(std::move(scope));
    }

    // Snapshot conditional guards (innermost first = reverse of stack).
    for (auto it = guardStack_.rbegin(); it != guardStack_.rend(); ++it) {
      ConditionalGuard guard;
      guard.conditionText = it->conditionText;
      guard.location = it->location;
      guard.inTrueBranch = it->inTrueBranch;
      guard.isAssertion = it->isAssertion;
      ctx.enclosingGuards.push_back(std::move(guard));
    }

    // Add any assertion guards seen in the current function scope.
    for (const auto &ag : assertionGuards_) {
      ConditionalGuard guard;
      guard.conditionText = ag.conditionText;
      guard.location = ag.location;
      guard.inTrueBranch = true;
      guard.isAssertion = true;
      ctx.enclosingGuards.push_back(std::move(guard));
    }

    // Extract noexcept spec of the caller.
    if (!funcStack_.empty()) {
      ctx.callerNoexcept = extractNoexceptSpec(funcStack_.back());
    }

    return ctx;
  }
};

// ============================================================================
// Consumer / Action / Factory (standard chain)
// ============================================================================

namespace {

class ControlFlowContextConsumer : public clang::ASTConsumer {
public:
  ControlFlowContextConsumer(ControlFlowIndex &index, const CallGraph &graph,
                             clang::SourceManager &sm)
      : visitor_(index, graph, sm) {}

  void HandleTranslationUnit(clang::ASTContext &ctx) override {
    visitor_.setASTContext(&ctx);
    visitor_.TraverseDecl(ctx.getTranslationUnitDecl());
  }

private:
  ControlFlowContextVisitor visitor_;
};

class ControlFlowContextAction : public clang::ASTFrontendAction {
public:
  ControlFlowContextAction(ControlFlowIndex &index, const CallGraph &graph)
      : index_(index), graph_(graph) {}

  std::unique_ptr<clang::ASTConsumer>
  CreateASTConsumer(clang::CompilerInstance &ci, llvm::StringRef) override {
    return std::make_unique<ControlFlowContextConsumer>(index_, graph_,
                                                        ci.getSourceManager());
  }

private:
  ControlFlowIndex &index_;
  const CallGraph &graph_;
};

class ControlFlowContextFactory
    : public clang::tooling::FrontendActionFactory {
public:
  ControlFlowContextFactory(ControlFlowIndex &index, const CallGraph &graph)
      : index_(index), graph_(graph) {}

  std::unique_ptr<clang::FrontendAction> create() override {
    return std::make_unique<ControlFlowContextAction>(index_, graph_);
  }

private:
  ControlFlowIndex &index_;
  const CallGraph &graph_;
};

} // anonymous namespace

// ============================================================================
// Public API
// ============================================================================

ControlFlowIndex
buildControlFlowIndex(const clang::tooling::CompilationDatabase &compDb,
                      const std::vector<std::string> &files,
                      const CallGraph &graph) {
  ControlFlowIndex index;
  auto tool = giga_drill::makeClangTool(compDb, files);
  ControlFlowContextFactory factory(index, graph);
  tool.run(&factory);
  return index;
}

} // namespace giga_drill

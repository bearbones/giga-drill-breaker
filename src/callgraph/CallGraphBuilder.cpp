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

#include "giga_drill/callgraph/CallGraphBuilder.h"
#include "giga_drill/compat/ToolAdjusters.h"

#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Tooling/CompilationDatabase.h"

namespace giga_drill {

// ============================================================================
// Helpers shared by both visitors
// ============================================================================

static std::string getFilePathHelper(clang::SourceManager &sm,
                                     clang::SourceLocation loc) {
  auto fileEntry =
      sm.getFileEntryRefForID(sm.getFileID(sm.getSpellingLoc(loc)));
  if (fileEntry)
    return std::string(fileEntry->getName());
  return "<unknown>";
}

static std::string formatLocationHelper(clang::SourceManager &sm,
                                        clang::SourceLocation loc) {
  auto sLoc = sm.getSpellingLoc(loc);
  auto file = sm.getFilename(sLoc);
  unsigned line = sm.getSpellingLineNumber(sLoc);
  unsigned col = sm.getSpellingColumnNumber(sLoc);
  return std::string(file) + ":" + std::to_string(line) + ":" +
         std::to_string(col);
}

// Stable synthetic name for a lambda closure: "lambda#file:line:col#enclosing".
// Both phases must compute the identical name so Phase 2 edges land on the
// Phase 1 node. The canonical location is the lambda's closure class
// begin-loc, which equals the LambdaExpr begin-loc (`[`).
static std::string lambdaQualifiedName(clang::SourceManager &sm,
                                       clang::SourceLocation loc,
                                       const std::string &enclosing) {
  std::string site = formatLocationHelper(sm, loc);
  std::string parent = enclosing.empty() ? std::string("<tu>") : enclosing;
  return "lambda#" + site + "#" + parent;
}

// Unwrap implicit/temporary/functional-cast wrappers and ask: does this
// expression denote a LambdaExpr? Handles the common
// std::function<…>(lambda) and std::thread(lambda, args…) argument shapes.
static const clang::LambdaExpr *asLambdaExpr(const clang::Expr *expr) {
  if (!expr)
    return nullptr;
  const clang::Expr *cur = expr->IgnoreParenImpCasts();
  while (cur) {
    if (auto *le = llvm::dyn_cast<clang::LambdaExpr>(cur))
      return le;
    if (auto *mt = llvm::dyn_cast<clang::MaterializeTemporaryExpr>(cur)) {
      cur = mt->getSubExpr();
      continue;
    }
    if (auto *bt = llvm::dyn_cast<clang::CXXBindTemporaryExpr>(cur)) {
      cur = bt->getSubExpr();
      continue;
    }
    if (auto *fc = llvm::dyn_cast<clang::CXXFunctionalCastExpr>(cur)) {
      cur = fc->getSubExpr();
      continue;
    }
    if (auto *cst = llvm::dyn_cast<clang::CastExpr>(cur)) {
      cur = cst->getSubExpr();
      continue;
    }
    if (auto *ce = llvm::dyn_cast<clang::CXXConstructExpr>(cur)) {
      if (ce->getNumArgs() >= 1) {
        cur = ce->getArg(0);
        continue;
      }
    }
    break;
  }
  return nullptr;
}

// Map a well-known concurrency-spawner qualified name to its ExecutionContext.
// Returns Synchronous for non-spawners; callers check for != Synchronous.
static ExecutionContext spawnerContextFor(llvm::StringRef qualifiedName) {
  if (qualifiedName == "std::thread::thread" ||
      qualifiedName == "std::jthread::jthread")
    return ExecutionContext::ThreadSpawn;
  if (qualifiedName == "std::async")
    return ExecutionContext::AsyncTask;
  if (qualifiedName == "std::packaged_task::packaged_task")
    return ExecutionContext::PackagedTask;
  if (qualifiedName == "std::invoke" || qualifiedName == "std::bind")
    return ExecutionContext::Invoke;
  return ExecutionContext::Synchronous;
}

// ============================================================================
// CallGraphIndexerVisitor (Phase 1)
// ============================================================================

CallGraphIndexerVisitor::CallGraphIndexerVisitor(CallGraph &graph,
                                                 clang::SourceManager &sm)
    : graph_(graph), sm_(sm) {}

std::string CallGraphIndexerVisitor::getFilePath(
    clang::SourceLocation loc) const {
  return getFilePathHelper(sm_, loc);
}

std::string CallGraphIndexerVisitor::formatLocation(
    clang::SourceLocation loc) const {
  return formatLocationHelper(sm_, loc);
}

std::string CallGraphIndexerVisitor::getCurrentFunction() const {
  if (funcStack_.empty())
    return "";
  auto *top = funcStack_.back();
  if (auto *md = llvm::dyn_cast<clang::CXXMethodDecl>(top)) {
    if (md->getParent() && md->getParent()->isLambda()) {
      // Find the nearest non-lambda enclosing function.
      std::string enclosing;
      for (auto it = funcStack_.rbegin() + 1; it != funcStack_.rend(); ++it) {
        auto *fd = *it;
        if (auto *mm = llvm::dyn_cast<clang::CXXMethodDecl>(fd)) {
          if (mm->getParent() && mm->getParent()->isLambda())
            continue;
        }
        enclosing = fd->getQualifiedNameAsString();
        break;
      }
      return lambdaQualifiedName(sm_, md->getParent()->getBeginLoc(),
                                 enclosing);
    }
  }
  return top->getQualifiedNameAsString();
}

bool CallGraphIndexerVisitor::TraverseFunctionDecl(
    clang::FunctionDecl *decl) {
  funcStack_.push_back(decl);
  bool result = RecursiveASTVisitor::TraverseFunctionDecl(decl);
  funcStack_.pop_back();
  return result;
}

bool CallGraphIndexerVisitor::TraverseCXXMethodDecl(
    clang::CXXMethodDecl *decl) {
  funcStack_.push_back(decl);
  bool result = RecursiveASTVisitor::TraverseCXXMethodDecl(decl);
  funcStack_.pop_back();
  return result;
}

bool CallGraphIndexerVisitor::TraverseCXXConstructorDecl(
    clang::CXXConstructorDecl *decl) {
  funcStack_.push_back(decl);
  bool result = RecursiveASTVisitor::TraverseCXXConstructorDecl(decl);
  funcStack_.pop_back();
  return result;
}

bool CallGraphIndexerVisitor::TraverseCXXDestructorDecl(
    clang::CXXDestructorDecl *decl) {
  funcStack_.push_back(decl);
  bool result = RecursiveASTVisitor::TraverseCXXDestructorDecl(decl);
  funcStack_.pop_back();
  return result;
}

bool CallGraphIndexerVisitor::VisitFunctionDecl(clang::FunctionDecl *decl) {
  if (decl->isImplicit())
    return true;

  // Skip function templates (we want concrete instantiations).
  if (decl->getDescribedFunctionTemplate())
    return true;

  // Avoid duplicates from redeclarations.
  if (decl->getPreviousDecl())
    return true;

  CallGraphNode node;
  node.qualifiedName = decl->getQualifiedNameAsString();
  node.file = getFilePath(decl->getLocation());
  node.line = sm_.getSpellingLineNumber(decl->getLocation());

  if (auto *method = llvm::dyn_cast<clang::CXXMethodDecl>(decl)) {
    node.isVirtual = method->isVirtual();
    if (auto *parent =
            llvm::dyn_cast<clang::CXXRecordDecl>(method->getParent()))
      node.enclosingClass = parent->getQualifiedNameAsString();
  }

  graph_.addNode(std::move(node));
  return true;
}

bool CallGraphIndexerVisitor::VisitCXXRecordDecl(
    clang::CXXRecordDecl *decl) {
  if (decl->isImplicit() || !decl->isThisDeclarationADefinition())
    return true;

  std::string className = decl->getQualifiedNameAsString();

  // Record base class relationships.
  for (const auto &base : decl->bases()) {
    auto *baseType = base.getType()->getAsCXXRecordDecl();
    if (baseType)
      graph_.addDerivedClass(baseType->getQualifiedNameAsString(), className);
  }

  // Record virtual method overrides.
  for (auto *method : decl->methods()) {
    if (!method->isVirtual() || method->isImplicit())
      continue;

    for (auto *overridden : method->overridden_methods()) {
      graph_.addMethodOverride(overridden->getQualifiedNameAsString(),
                               method->getQualifiedNameAsString());
    }
  }

  // Compute effective implementations for concrete classes.
  if (!decl->isAbstract())
    computeEffectiveImpls(decl);

  return true;
}

void CallGraphIndexerVisitor::computeEffectiveImpls(
    const clang::CXXRecordDecl *cls) {
  std::string className = cls->getQualifiedNameAsString();
  std::set<std::string> handledMethodNames;

  // Walk from most-derived (cls) upward through bases.
  // The first implementation found for a method name is the effective one.
  std::vector<const clang::CXXRecordDecl *> hierarchy;
  hierarchy.push_back(cls);

  for (size_t i = 0; i < hierarchy.size(); ++i) {
    const auto *current = hierarchy[i];

    for (auto *method : current->methods()) {
      if (!method->isVirtual() || method->isImplicit())
        continue;

      std::string methodName = method->getNameAsString();
      if (handledMethodNames.count(methodName))
        continue;

      if (!method->isPureVirtual()) {
        handledMethodNames.insert(methodName);
        graph_.addEffectiveImpl(className,
                                method->getQualifiedNameAsString());
      }
    }

    // Add base classes to walk.
    for (const auto &base : current->bases()) {
      auto *baseDecl = base.getType()->getAsCXXRecordDecl();
      if (baseDecl && baseDecl->isThisDeclarationADefinition())
        hierarchy.push_back(baseDecl);
    }
  }
}

bool CallGraphIndexerVisitor::TraverseLambdaExpr(clang::LambdaExpr *expr) {
  // While inside the lambda body, bodyFunc_ should be the call operator so
  // nested VisitReturnStmt / visitor methods attribute to the lambda node.
  auto *op = expr->getCallOperator();
  if (op)
    funcStack_.push_back(op);
  bool result = RecursiveASTVisitor::TraverseLambdaExpr(expr);
  if (op)
    funcStack_.pop_back();
  return result;
}

bool CallGraphIndexerVisitor::VisitLambdaExpr(clang::LambdaExpr *expr) {
  // At this point TraverseLambdaExpr has already pushed the call operator, so
  // skip the top frame when searching for the real enclosing function.
  std::string enclosing;
  auto begin = funcStack_.rbegin();
  if (begin != funcStack_.rend()) {
    if (auto *md = llvm::dyn_cast<clang::CXXMethodDecl>(*begin)) {
      if (md->getParent() && md->getParent()->isLambda())
        ++begin;
    }
  }
  for (auto it = begin; it != funcStack_.rend(); ++it) {
    auto *fd = *it;
    if (auto *md = llvm::dyn_cast<clang::CXXMethodDecl>(fd)) {
      if (md->getParent() && md->getParent()->isLambda())
        continue;
    }
    enclosing = fd->getQualifiedNameAsString();
    break;
  }

  CallGraphNode node;
  node.qualifiedName =
      lambdaQualifiedName(sm_, expr->getBeginLoc(), enclosing);
  node.file = getFilePath(expr->getBeginLoc());
  node.line = sm_.getSpellingLineNumber(expr->getBeginLoc());
  node.enclosingClass = enclosing;
  graph_.addNode(std::move(node));
  return true;
}

bool CallGraphIndexerVisitor::VisitReturnStmt(clang::ReturnStmt *stmt) {
  auto *retVal = stmt->getRetValue();
  if (!retVal)
    return true;

  // Check if returning a function reference.
  auto *expr = retVal->IgnoreParenImpCasts();
  if (auto *dre = llvm::dyn_cast<clang::DeclRefExpr>(expr)) {
    if (auto *funcDecl = llvm::dyn_cast<clang::FunctionDecl>(dre->getDecl())) {
      std::string enclosing = getCurrentFunction();
      if (!enclosing.empty()) {
        graph_.addFunctionReturn(enclosing,
                                 funcDecl->getQualifiedNameAsString());
      }
    }
  }

  return true;
}

// ============================================================================
// CallGraphEdgeVisitor (Phase 2)
// ============================================================================

CallGraphEdgeVisitor::CallGraphEdgeVisitor(CallGraph &graph,
                                           clang::SourceManager &sm)
    : graph_(graph), sm_(sm) {}

std::string CallGraphEdgeVisitor::getFilePath(
    clang::SourceLocation loc) const {
  return getFilePathHelper(sm_, loc);
}

std::string CallGraphEdgeVisitor::formatLocation(
    clang::SourceLocation loc) const {
  return formatLocationHelper(sm_, loc);
}

std::string CallGraphEdgeVisitor::getCurrentFunction() const {
  if (funcStack_.empty())
    return "";
  auto *top = funcStack_.back();
  if (auto *md = llvm::dyn_cast<clang::CXXMethodDecl>(top)) {
    if (md->getParent() && md->getParent()->isLambda()) {
      std::string enclosing;
      for (auto it = funcStack_.rbegin() + 1; it != funcStack_.rend(); ++it) {
        auto *fd = *it;
        if (auto *mm = llvm::dyn_cast<clang::CXXMethodDecl>(fd)) {
          if (mm->getParent() && mm->getParent()->isLambda())
            continue;
        }
        enclosing = fd->getQualifiedNameAsString();
        break;
      }
      return lambdaQualifiedName(sm_, md->getParent()->getBeginLoc(),
                                 enclosing);
    }
  }
  return top->getQualifiedNameAsString();
}

bool CallGraphEdgeVisitor::isInUserCode(clang::SourceLocation loc) const {
  if (loc.isInvalid())
    return false;
  return !sm_.isInSystemHeader(sm_.getSpellingLoc(loc));
}

bool CallGraphEdgeVisitor::TraverseFunctionDecl(clang::FunctionDecl *decl) {
  funcStack_.push_back(decl);
  bool result = RecursiveASTVisitor::TraverseFunctionDecl(decl);
  funcStack_.pop_back();
  return result;
}

bool CallGraphEdgeVisitor::TraverseCXXMethodDecl(
    clang::CXXMethodDecl *decl) {
  funcStack_.push_back(decl);
  bool result = RecursiveASTVisitor::TraverseCXXMethodDecl(decl);
  funcStack_.pop_back();
  return result;
}

bool CallGraphEdgeVisitor::TraverseCXXConstructorDecl(
    clang::CXXConstructorDecl *decl) {
  funcStack_.push_back(decl);
  bool result = RecursiveASTVisitor::TraverseCXXConstructorDecl(decl);
  funcStack_.pop_back();
  return result;
}

bool CallGraphEdgeVisitor::TraverseLambdaExpr(clang::LambdaExpr *expr) {
  auto *op = expr->getCallOperator();
  if (op)
    funcStack_.push_back(op);
  bool result = RecursiveASTVisitor::TraverseLambdaExpr(expr);
  if (op)
    funcStack_.pop_back();
  return result;
}

bool CallGraphEdgeVisitor::TraverseCXXDestructorDecl(
    clang::CXXDestructorDecl *decl) {
  funcStack_.push_back(decl);
  bool result = RecursiveASTVisitor::TraverseCXXDestructorDecl(decl);
  funcStack_.pop_back();
  return result;
}

std::string CallGraphEdgeVisitor::enclosingNonLambdaName() const {
  for (auto it = funcStack_.rbegin(); it != funcStack_.rend(); ++it) {
    auto *fd = *it;
    if (auto *md = llvm::dyn_cast<clang::CXXMethodDecl>(fd)) {
      if (md->getParent() && md->getParent()->isLambda())
        continue;
    }
    return fd->getQualifiedNameAsString();
  }
  return "";
}

void CallGraphEdgeVisitor::processCallableArgs(
    llvm::ArrayRef<clang::Expr *> args, const std::string &caller,
    clang::SourceLocation callSite, ExecutionContext spawnerCtx) {
  const bool isSpawner = spawnerCtx != ExecutionContext::Synchronous;
  const EdgeKind ptrEdgeKind =
      isSpawner ? EdgeKind::ThreadEntry : EdgeKind::FunctionPointer;
  const EdgeKind lambdaEdgeKind =
      isSpawner ? EdgeKind::ThreadEntry : EdgeKind::LambdaCall;
  const std::string siteStr = formatLocation(callSite);

  for (auto *argExpr : args) {
    if (!argExpr)
      continue;
    auto *arg = argExpr->IgnoreParenImpCasts();

    // Lambda passed as argument (direct, or wrapped in std::function /
    // packaged_task / bind trampolines).
    if (auto *le = asLambdaExpr(argExpr)) {
      std::string lambdaName = lambdaQualifiedName(
          sm_, le->getBeginLoc(), enclosingNonLambdaName());
      graph_.addEdge({caller, lambdaName, lambdaEdgeKind, Confidence::Proven,
                      siteStr, 1, spawnerCtx});
      continue;
    }

    // Unwrap `&f` (explicit address-take of a function or member function).
    if (auto *uo = llvm::dyn_cast<clang::UnaryOperator>(arg)) {
      if (uo->getOpcode() == clang::UO_AddrOf)
        arg = uo->getSubExpr()->IgnoreParenImpCasts();
    }

    if (auto *dre = llvm::dyn_cast<clang::DeclRefExpr>(arg)) {
      if (auto *funcDecl =
              llvm::dyn_cast<clang::FunctionDecl>(dre->getDecl())) {
        graph_.addEdge({caller, funcDecl->getQualifiedNameAsString(),
                        ptrEdgeKind, Confidence::Proven, siteStr, 1,
                        spawnerCtx});
        handledRefs_.insert(dre);
      } else if (auto *varDecl =
                     llvm::dyn_cast<clang::VarDecl>(dre->getDecl())) {
        auto fnIt = varFuncSources_.find(varDecl);
        if (fnIt != varFuncSources_.end()) {
          for (const auto &funcName : fnIt->second) {
            graph_.addEdge({caller, funcName, ptrEdgeKind,
                            Confidence::Proven, siteStr, 2, spawnerCtx});
          }
          handledRefs_.insert(dre);
        }
        auto lamIt = varLambdaSources_.find(varDecl);
        if (lamIt != varLambdaSources_.end()) {
          graph_.addEdge({caller, lamIt->second, lambdaEdgeKind,
                          Confidence::Proven, siteStr, 1, spawnerCtx});
          handledRefs_.insert(dre);
        }
      }
    }
  }
}

bool CallGraphEdgeVisitor::VisitCallExpr(clang::CallExpr *expr) {
  std::string caller = getCurrentFunction();
  if (caller.empty())
    return true;

  // Skip calls from within system headers.
  if (!isInUserCode(expr->getBeginLoc()))
    return true;

  // Record the callee DeclRefExpr so VisitDeclRefExpr won't double-count it.
  if (auto *calleeExpr = expr->getCallee()) {
    if (auto *dre = llvm::dyn_cast<clang::DeclRefExpr>(
            calleeExpr->IgnoreParenImpCasts())) {
      handledRefs_.insert(dre);
    }
  }

  // Spawner? (std::async, std::invoke, std::bind, etc. — CallExpr forms.)
  ExecutionContext spawnerCtx = ExecutionContext::Synchronous;
  if (auto *direct = expr->getDirectCallee()) {
    spawnerCtx = spawnerContextFor(direct->getQualifiedNameAsString());
  }

  // Process arguments: detect function pointers and lambdas as callables.
  std::vector<clang::Expr *> args;
  args.reserve(expr->getNumArgs());
  for (unsigned i = 0; i < expr->getNumArgs(); ++i)
    args.push_back(expr->getArg(i));
  processCallableArgs(args, caller, expr->getBeginLoc(), spawnerCtx);

  auto *callee = expr->getDirectCallee();
  if (!callee)
    return true;

  // Handle CXXMemberCallExpr for virtual dispatch.
  if (auto *memberCall = llvm::dyn_cast<clang::CXXMemberCallExpr>(expr)) {
    auto *methodDecl = memberCall->getMethodDecl();
    if (methodDecl && methodDecl->isVirtual()) {
      handleVirtualDispatch(caller, methodDecl,
                            expr->getBeginLoc());
      return true;
    }
  }

  // Handle CXXOperatorCallExpr.
  if (llvm::isa<clang::CXXOperatorCallExpr>(expr)) {
    graph_.addEdge({caller, callee->getQualifiedNameAsString(),
                    EdgeKind::OperatorCall, Confidence::Proven,
                    formatLocation(expr->getBeginLoc()), 0});
    return true;
  }

  // Detect std::make_unique<T> / std::make_shared<T> as constructing T.
  if (auto *specArgs = callee->getTemplateSpecializationArgs()) {
    std::string calleeName = callee->getQualifiedNameAsString();
    if (calleeName.find("make_unique") != std::string::npos ||
        calleeName.find("make_shared") != std::string::npos) {
      if (specArgs->size() > 0 &&
          specArgs->get(0).getKind() == clang::TemplateArgument::Type) {
        auto constructedType = specArgs->get(0).getAsType();
        if (auto *recordDecl = constructedType->getAsCXXRecordDecl()) {
          if (recordDecl->isThisDeclarationADefinition()) {
            std::string typeName = recordDecl->getQualifiedNameAsString();
            // Add a constructor edge for the constructed type.
            for (auto *ctor : recordDecl->ctors()) {
              if (!ctor->isImplicit() || ctor->isCopyOrMoveConstructor())
                continue;
              graph_.addNode(
                  {ctor->getQualifiedNameAsString(),
                   getFilePath(ctor->getLocation()),
                   sm_.getSpellingLineNumber(ctor->getLocation()), false, false,
                   typeName});
              graph_.addEdge({caller, ctor->getQualifiedNameAsString(),
                              EdgeKind::ConstructorCall, Confidence::Proven,
                              formatLocation(expr->getBeginLoc()), 0});
            }
            // Also just add a generic constructor node for the type.
            std::string ctorName = typeName + "::" +
                                   recordDecl->getNameAsString();
            graph_.addNode({ctorName, getFilePath(recordDecl->getLocation()),
                            sm_.getSpellingLineNumber(recordDecl->getLocation()),
                            false, false, typeName});
            graph_.addEdge({caller, ctorName, EdgeKind::ConstructorCall,
                            Confidence::Proven,
                            formatLocation(expr->getBeginLoc()), 0});
          }
        }
      }
    }
  }

  // Regular direct call.
  graph_.addEdge({caller, callee->getQualifiedNameAsString(),
                  EdgeKind::DirectCall, Confidence::Proven,
                  formatLocation(expr->getBeginLoc()), 0});

  return true;
}

void CallGraphEdgeVisitor::handleVirtualDispatch(
    const std::string &caller, clang::CXXMethodDecl *method,
    clang::SourceLocation loc) {
  std::string baseMethodName = method->getQualifiedNameAsString();
  std::string site = formatLocation(loc);

  // Add Plausible edge to the base method itself (if not pure virtual).
  if (!method->isPureVirtual()) {
    graph_.addEdge({caller, baseMethodName, EdgeKind::VirtualDispatch,
                    Confidence::Plausible, site, 0});
  }

  // Add Plausible edges to all known overrides.
  auto overrides = graph_.getOverrides(baseMethodName);
  for (const auto &overrideName : overrides) {
    graph_.addEdge({caller, overrideName, EdgeKind::VirtualDispatch,
                    Confidence::Plausible, site, 0});
  }

  // Also check overrides of methods that this method itself overrides
  // (to catch the full override chain).
  for (auto *overridden : method->overridden_methods()) {
    auto moreOverrides =
        graph_.getOverrides(overridden->getQualifiedNameAsString());
    for (const auto &overrideName : moreOverrides) {
      if (overrideName != baseMethodName) {
        graph_.addEdge({caller, overrideName, EdgeKind::VirtualDispatch,
                        Confidence::Plausible, site, 0});
      }
    }
  }
}

bool CallGraphEdgeVisitor::VisitCXXConstructExpr(
    clang::CXXConstructExpr *expr) {
  std::string caller = getCurrentFunction();
  if (caller.empty())
    return true;

  if (!isInUserCode(expr->getBeginLoc()))
    return true;

  auto *ctor = expr->getConstructor();
  if (!ctor || ctor->isImplicit())
    return true;

  // Concurrency spawner via constructor (e.g. `std::thread t(&fn, arg)`).
  // Emit ThreadEntry edges for each callable argument in addition to the
  // normal ConstructorCall edge.
  std::string ctorName = ctor->getQualifiedNameAsString();
  ExecutionContext spawnerCtx = spawnerContextFor(ctorName);
  if (spawnerCtx != ExecutionContext::Synchronous) {
    std::vector<clang::Expr *> args;
    args.reserve(expr->getNumArgs());
    for (unsigned i = 0; i < expr->getNumArgs(); ++i)
      args.push_back(expr->getArg(i));
    processCallableArgs(args, caller, expr->getBeginLoc(), spawnerCtx);
  }

  // Add constructor edge.
  graph_.addNode({ctorName, getFilePath(ctor->getLocation()),
                  sm_.getSpellingLineNumber(ctor->getLocation()), false, false,
                  ctor->getParent()->getQualifiedNameAsString()});
  graph_.addEdge({caller, ctorName, EdgeKind::ConstructorCall,
                  Confidence::Proven, formatLocation(expr->getBeginLoc()), 0});

  return true;
}

bool CallGraphEdgeVisitor::VisitVarDecl(clang::VarDecl *decl) {
  if (!decl->isLocalVarDecl())
    return true;

  std::string caller = getCurrentFunction();
  if (caller.empty())
    return true;

  // Track variables assigned from functions returning function pointers.
  if (auto *init = decl->getInit()) {
    auto *initExpr = init->IgnoreParenImpCasts();
    if (auto *callExpr = llvm::dyn_cast<clang::CallExpr>(initExpr)) {
      auto *callee = callExpr->getDirectCallee();
      if (callee) {
        auto returns =
            graph_.getFunctionReturns(callee->getQualifiedNameAsString());
        if (!returns.empty())
          varFuncSources_[decl] = std::move(returns);
      }
    }

    // Track locals initialized from a lambda expression (e.g.
    // `auto cb = [=](int x){ ... };` then later `std::thread(cb)`).
    if (auto *le = asLambdaExpr(init)) {
      std::string enclosing;
      for (auto it = funcStack_.rbegin(); it != funcStack_.rend(); ++it) {
        auto *fd = *it;
        if (auto *md = llvm::dyn_cast<clang::CXXMethodDecl>(fd)) {
          if (md->getParent() && md->getParent()->isLambda())
            continue;
        }
        enclosing = fd->getQualifiedNameAsString();
        break;
      }
      varLambdaSources_[decl] =
          lambdaQualifiedName(sm_, le->getBeginLoc(), enclosing);
    }
  }

  // Add "concrete type knowledge" edges for polymorphic local variables.
  auto *recordDecl = decl->getType()->getAsCXXRecordDecl();
  if (!recordDecl || !recordDecl->isPolymorphic())
    return true;
  if (recordDecl->isAbstract())
    return true;
  if (!recordDecl->isThisDeclarationADefinition())
    return true;

  addConcreteTypeEdges(caller, recordDecl, decl->getLocation());
  return true;
}

void CallGraphEdgeVisitor::addConcreteTypeEdges(
    const std::string &caller, const clang::CXXRecordDecl *cls,
    clang::SourceLocation loc) {
  std::string site = formatLocation(loc);
  std::set<std::string> handledMethodNames;

  // Walk from most-derived class upward to find effective implementations.
  std::vector<const clang::CXXRecordDecl *> hierarchy;
  hierarchy.push_back(cls);

  for (size_t i = 0; i < hierarchy.size(); ++i) {
    const auto *current = hierarchy[i];

    for (auto *method : current->methods()) {
      if (!method->isVirtual() || method->isImplicit())
        continue;

      std::string methodName = method->getNameAsString();
      if (handledMethodNames.count(methodName))
        continue;

      if (!method->isPureVirtual()) {
        handledMethodNames.insert(methodName);
        graph_.addEdge({caller, method->getQualifiedNameAsString(),
                        EdgeKind::VirtualDispatch, Confidence::Proven, site,
                        0});
      }
    }

    // Also add destructor edges.
    if (auto *dtor = current->getDestructor()) {
      std::string dtorName = dtor->getQualifiedNameAsString();
      if (!handledMethodNames.count("~")) {
        graph_.addNode({dtorName, getFilePath(dtor->getLocation()),
                        sm_.getSpellingLineNumber(dtor->getLocation()), false,
                        dtor->isVirtual(),
                        current->getQualifiedNameAsString()});
        graph_.addEdge({caller, dtorName, EdgeKind::DestructorCall,
                        Confidence::Proven, site, 0});
      }
    }

    for (const auto &base : current->bases()) {
      auto *baseDecl = base.getType()->getAsCXXRecordDecl();
      if (baseDecl && baseDecl->isThisDeclarationADefinition())
        hierarchy.push_back(baseDecl);
    }
  }
}

bool CallGraphEdgeVisitor::VisitDeclRefExpr(clang::DeclRefExpr *expr) {
  // Skip if already handled as callee or function-pointer argument.
  if (handledRefs_.count(expr))
    return true;

  auto *funcDecl = llvm::dyn_cast<clang::FunctionDecl>(expr->getDecl());
  if (!funcDecl)
    return true;

  std::string caller = getCurrentFunction();
  if (caller.empty())
    return true;

  if (!isInUserCode(expr->getBeginLoc()))
    return true;

  // This is a function reference in a non-call, non-argument context.
  // Treat as address-taken: Plausible edge.
  graph_.addEdge({caller, funcDecl->getQualifiedNameAsString(),
                  EdgeKind::FunctionPointer, Confidence::Plausible,
                  formatLocation(expr->getBeginLoc()), 0});

  return true;
}

// ============================================================================
// Consumer / Action / Factory
// ============================================================================

CallGraphBuilderConsumer::CallGraphBuilderConsumer(CallGraph &graph,
                                                   clang::SourceManager &sm)
    : indexer_(graph, sm), edgeBuilder_(graph, sm) {}

void CallGraphBuilderConsumer::HandleTranslationUnit(
    clang::ASTContext &context) {
  // Phase 1: Index declarations and hierarchy.
  indexer_.setASTContext(&context);
  indexer_.TraverseDecl(context.getTranslationUnitDecl());

  // Phase 2: Build edges.
  edgeBuilder_.setASTContext(&context);
  edgeBuilder_.TraverseDecl(context.getTranslationUnitDecl());
}

CallGraphBuilderAction::CallGraphBuilderAction(CallGraph &graph)
    : graph_(graph) {}

std::unique_ptr<clang::ASTConsumer>
CallGraphBuilderAction::CreateASTConsumer(clang::CompilerInstance &ci,
                                          llvm::StringRef /*file*/) {
  return std::make_unique<CallGraphBuilderConsumer>(graph_,
                                                    ci.getSourceManager());
}

CallGraphBuilderFactory::CallGraphBuilderFactory(CallGraph &graph)
    : graph_(graph) {}

std::unique_ptr<clang::FrontendAction> CallGraphBuilderFactory::create() {
  return std::make_unique<CallGraphBuilderAction>(graph_);
}

// ============================================================================
// Multi-TU builder
// ============================================================================

namespace {

class IndexerOnlyConsumer : public clang::ASTConsumer {
public:
  IndexerOnlyConsumer(CallGraph &graph, clang::SourceManager &sm)
      : visitor_(graph, sm) {}
  void HandleTranslationUnit(clang::ASTContext &ctx) override {
    visitor_.setASTContext(&ctx);
    visitor_.TraverseDecl(ctx.getTranslationUnitDecl());
  }

private:
  CallGraphIndexerVisitor visitor_;
};

class IndexerOnlyAction : public clang::ASTFrontendAction {
public:
  explicit IndexerOnlyAction(CallGraph &g) : graph_(g) {}
  std::unique_ptr<clang::ASTConsumer>
  CreateASTConsumer(clang::CompilerInstance &ci, llvm::StringRef) override {
    return std::make_unique<IndexerOnlyConsumer>(graph_,
                                                 ci.getSourceManager());
  }

private:
  CallGraph &graph_;
};

class IndexerOnlyFactory : public clang::tooling::FrontendActionFactory {
public:
  explicit IndexerOnlyFactory(CallGraph &g) : graph_(g) {}
  std::unique_ptr<clang::FrontendAction> create() override {
    return std::make_unique<IndexerOnlyAction>(graph_);
  }

private:
  CallGraph &graph_;
};

class EdgeOnlyConsumer : public clang::ASTConsumer {
public:
  EdgeOnlyConsumer(CallGraph &graph, clang::SourceManager &sm)
      : visitor_(graph, sm) {}
  void HandleTranslationUnit(clang::ASTContext &ctx) override {
    visitor_.setASTContext(&ctx);
    visitor_.TraverseDecl(ctx.getTranslationUnitDecl());
  }

private:
  CallGraphEdgeVisitor visitor_;
};

class EdgeOnlyAction : public clang::ASTFrontendAction {
public:
  explicit EdgeOnlyAction(CallGraph &g) : graph_(g) {}
  std::unique_ptr<clang::ASTConsumer>
  CreateASTConsumer(clang::CompilerInstance &ci, llvm::StringRef) override {
    return std::make_unique<EdgeOnlyConsumer>(graph_, ci.getSourceManager());
  }

private:
  CallGraph &graph_;
};

class EdgeOnlyFactory : public clang::tooling::FrontendActionFactory {
public:
  explicit EdgeOnlyFactory(CallGraph &g) : graph_(g) {}
  std::unique_ptr<clang::FrontendAction> create() override {
    return std::make_unique<EdgeOnlyAction>(graph_);
  }

private:
  CallGraph &graph_;
};

} // anonymous namespace

CallGraph buildCallGraph(const clang::tooling::CompilationDatabase &compDb,
                         const std::vector<std::string> &files) {
  CallGraph graph;

  // Pass 1: Index all declarations and class hierarchy.
  {
    auto tool = giga_drill::makeClangTool(compDb, files);
    IndexerOnlyFactory factory(graph);
    tool.run(&factory);
  }

  // Pass 2: Build edges with full knowledge.
  {
    auto tool = giga_drill::makeClangTool(compDb, files);
    EdgeOnlyFactory factory(graph);
    tool.run(&factory);
  }

  return graph;
}

} // namespace giga_drill

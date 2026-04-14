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

#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace clang {
namespace tooling {
class CompilationDatabase;
} // namespace tooling
} // namespace clang

namespace giga_drill {

class CallGraph;

// ============================================================================
// Exception handling context
// ============================================================================

struct CatchHandlerInfo {
  std::string caughtType; // Qualified type name, or "" for catch(...)
  bool isCatchAll = false;
  std::string location; // file:line:col of the catch keyword
};

struct TryCatchScope {
  std::string tryLocation;       // file:line:col of the try keyword
  std::string enclosingFunction; // Qualified name of containing function
  std::vector<CatchHandlerInfo> handlers;
  unsigned nestingDepth = 0; // 0 = outermost try in function
};

enum class NoexceptSpec {
  None,          // No noexcept specifier
  Noexcept,      // noexcept or noexcept(true)
  NoexceptFalse, // noexcept(false)
  ThrowNone,     // throw() (C++98 dynamic exception spec)
  Unknown        // Dependent or unresolved
};

// ============================================================================
// Conditional guard context
// ============================================================================

struct ConditionalGuard {
  std::string conditionText; // Source text of the condition
  std::string location;      // file:line:col of the if/assert
  bool inTrueBranch = true;  // true = if-branch, false = else-branch
  bool isAssertion = false;  // assert(), DCHECK(), etc.
};

// ============================================================================
// Per-call-site record
// ============================================================================

struct CallSiteContext {
  std::string callerName; // Matches CallGraphEdge::callerName
  std::string calleeName; // What's being called
  std::string callSite;   // file:line:col (join key with CallGraphEdge)

  // Try/catch scopes enclosing this call, innermost first.
  std::vector<TryCatchScope> enclosingTryCatches;

  // Conditional guards enclosing this call, innermost first.
  std::vector<ConditionalGuard> enclosingGuards;

  // Noexcept spec of the caller function.
  NoexceptSpec callerNoexcept = NoexceptSpec::None;

  // Whether inside a catch block body (re-throw context).
  bool insideCatchBlock = false;
};

// ============================================================================
// ControlFlowIndex — parallel index alongside CallGraph
// ============================================================================

class ControlFlowIndex {
public:
  void addCallSiteContext(CallSiteContext ctx);

  // Look up context at a specific call site (file:line:col).
  const CallSiteContext *contextAtSite(const std::string &callSite) const;

  // All contexts where calleeName is the target.
  std::vector<const CallSiteContext *>
  contextsForCallee(const std::string &calleeName) const;

  // All contexts where callerName is the source.
  std::vector<const CallSiteContext *>
  contextsForCaller(const std::string &callerName) const;

  // All call sites targeting calleeName that are inside a try/catch.
  std::vector<const CallSiteContext *>
  protectedCallsTo(const std::string &calleeName) const;

  // All call sites targeting calleeName that are NOT inside a try/catch.
  std::vector<const CallSiteContext *>
  unprotectedCallsTo(const std::string &calleeName) const;

  size_t size() const { return contexts_.size(); }

  // All stored contexts (for dump mode).
  std::vector<const CallSiteContext *> allContexts() const;

private:
  std::vector<CallSiteContext> contexts_;
  std::unordered_map<std::string, std::vector<size_t>> byCallee_;
  std::unordered_map<std::string, std::vector<size_t>> byCaller_;
  std::unordered_map<std::string, size_t> bySite_;
};

// Build a ControlFlowIndex from a compilation database (Phase 3, after call
// graph construction). The CallGraph is used to resolve callee noexcept specs.
ControlFlowIndex
buildControlFlowIndex(const clang::tooling::CompilationDatabase &compDb,
                      const std::vector<std::string> &files,
                      const CallGraph &graph);

} // namespace giga_drill

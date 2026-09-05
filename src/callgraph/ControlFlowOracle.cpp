// Copyright (c) 2026 The vycor-cpp Authors
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

#include "vycor/callgraph/ControlFlowOracle.h"
#include "vycor/callgraph/CallGraph.h"
#include "vycor/ext/Extensions.h"

#include <algorithm>
#include <functional>
#include <set>
#include <sstream>

namespace vycor {

// ============================================================================
// Hardcoded std exception hierarchy for type matching
// ============================================================================

// Maps derived exception types to their known bases.
static const std::vector<std::pair<std::string, std::string>> &
stdExceptionHierarchy() {
  static const std::vector<std::pair<std::string, std::string>> hierarchy = {
      {"std::bad_alloc", "std::exception"},
      {"std::bad_cast", "std::exception"},
      {"std::bad_typeid", "std::exception"},
      {"std::bad_function_call", "std::exception"},
      {"std::bad_weak_ptr", "std::exception"},
      {"std::bad_array_new_length", "std::bad_alloc"},
      {"std::logic_error", "std::exception"},
      {"std::runtime_error", "std::exception"},
      {"std::domain_error", "std::logic_error"},
      {"std::invalid_argument", "std::logic_error"},
      {"std::length_error", "std::logic_error"},
      {"std::out_of_range", "std::logic_error"},
      {"std::overflow_error", "std::runtime_error"},
      {"std::underflow_error", "std::runtime_error"},
      {"std::range_error", "std::runtime_error"},
      {"std::system_error", "std::runtime_error"},
      {"std::ios_base::failure", "std::system_error"},
      {"std::filesystem::filesystem_error", "std::system_error"},
  };
  return hierarchy;
}

// Check if thrownType is-a caughtType (including through base classes).
static bool isSubtypeOf(const std::string &thrownType,
                        const std::string &caughtType) {
  if (thrownType == caughtType)
    return true;

  // Strip reference and const qualifiers for matching.
  auto strip = [](const std::string &t) -> std::string {
    std::string s = t;
    // Remove trailing & or &&.
    while (!s.empty() && s.back() == '&')
      s.pop_back();
    // Remove leading "const " and trailing whitespace.
    if (s.compare(0, 6, "const ") == 0)
      s = s.substr(6);
    while (!s.empty() && s.back() == ' ')
      s.pop_back();
    return s;
  };

  std::string thrown = strip(thrownType);
  std::string caught = strip(caughtType);
  if (thrown == caught)
    return true;

  // Walk the hardcoded hierarchy.
  std::string current = thrown;
  std::set<std::string> visited;
  while (visited.insert(current).second) {
    for (const auto &pair : stdExceptionHierarchy()) {
      if (pair.first == current) {
        if (pair.second == caught)
          return true;
        current = pair.second;
        break;
      }
    }
    // If we didn't find current in the hierarchy, stop.
    if (current == thrown || visited.count(current))
      break;
    thrown = current; // Continue walking up.
  }

  return false;
}

// ============================================================================
// Constructor
// ============================================================================

ControlFlowOracle::ControlFlowOracle(const CallGraph &graph,
                                     const ControlFlowIndex &cfIndex)
    : graph_(graph), cfIndex_(cfIndex) {}

// ============================================================================
// Q1: queryCallSite
// ============================================================================

CallSiteExceptionInfo
ControlFlowOracle::queryCallSite(const std::string &callSite) const {
  CallSiteExceptionInfo info;
  info.callSite = callSite;

  const auto ctx = cfIndex_.contextAtSite(callSite);
  if (!ctx)
    return info;

  info.caller = ctx->callerName;
  info.callee = ctx->calleeName;
  info.isUnderTryCatch = !ctx->enclosingTryCatches.empty();
  info.enclosingScopes = ctx->enclosingTryCatches;
  info.enclosingGuards = ctx->enclosingGuards;
  info.callerNoexcept = ctx->callerNoexcept;
  info.wouldTerminateIfThrows =
      (ctx->callerNoexcept == NoexceptSpec::Noexcept);

  return info;
}

// ============================================================================
// Path finding: reverse DFS from target to entries
// ============================================================================

void ControlFlowOracle::findPathsToTarget(
    const std::string &target,
    const std::vector<std::string> &entryPoints,
    unsigned maxPaths,
    std::vector<std::vector<std::string>> &outPaths) const {

  std::set<std::string> entries(entryPoints.begin(), entryPoints.end());

  // DFS state: current path being explored (target is at back, entries at front
  // when found).
  struct Frame {
    std::string func;
    size_t callerIdx; // index into callersOf result
  };

  // Build reverse path from target. We store the path reversed (target first),
  // then reverse at the end.
  std::vector<std::string> currentPath;
  std::set<std::string> onPath; // Cycle detection.

  // Recursive lambda for DFS.
  std::function<void(const std::string &)> dfs =
      [&](const std::string &node) {
        if (outPaths.size() >= maxPaths)
          return;

        currentPath.push_back(node);
        onPath.insert(node);

        if (entries.count(node)) {
          // Found a path from entry to target — store it reversed.
          std::vector<std::string> path(currentPath.rbegin(),
                                        currentPath.rend());
          outPaths.push_back(std::move(path));
        } else {
          // Walk callers.
          auto callers = graph_.callersOf(node);
          for (const auto &edge : callers) {
            if (onPath.count(edge.callerName))
              continue; // Skip cycles.
            dfs(edge.callerName);
            if (outPaths.size() >= maxPaths)
              break;
          }
        }

        currentPath.pop_back();
        onPath.erase(node);
      };

  dfs(target);
}

// ============================================================================
// Exception type matching
// ============================================================================

bool ControlFlowOracle::isCaughtByScope(const std::string &exceptionType,
                                        const TryCatchScope &scope) {
  for (const auto &handler : scope.handlers) {
    if (handler.isCatchAll)
      return true;
    if (isSubtypeOf(exceptionType, handler.caughtType))
      return true;
  }
  return false;
}

// ============================================================================
// Q2: queryExceptionProtection
// ============================================================================

ExceptionPathResult ControlFlowOracle::queryExceptionProtection(
    const std::string &functionName, const std::string &exceptionType,
    const std::vector<std::string> &entryPoints) const {

  ExceptionPathResult result;

  // Find all paths from entries to functionName.
  std::vector<std::vector<std::string>> rawPaths;
  findPathsToTarget(functionName, entryPoints, 100, rawPaths);

  if (rawPaths.empty()) {
    result.protection = Protection::Unknown;
    result.summary =
        buildSummary(result, functionName, exceptionType);
    return result;
  }

  unsigned caughtCount = 0;
  unsigned uncaughtCount = 0;

  for (const auto &chain : rawPaths) {
    PathInfo pi;
    pi.callChain = chain;
    pi.isCaught = false;

    // Walk the path from entry toward target, collecting try/catch context.
    // For each edge (caller -> callee), check the call site context.
    for (size_t i = 0; i + 1 < chain.size(); ++i) {
      const std::string &caller = chain[i];
      const std::string &callee = chain[i + 1];

      // Find the call site context for this edge.
      auto contexts = cfIndex_.contextsForCaller(caller);
      for (const auto &ctx : contexts) {
        if (ctx.calleeName != callee)
          continue;

        // Collect try/catch scopes on this edge.
        for (const auto &scope : ctx.enclosingTryCatches) {
          pi.tryCatchesOnPath.push_back(scope);
          if (!pi.isCaught && isCaughtByScope(exceptionType, scope)) {
            pi.isCaught = true;
            pi.caughtAt = scope.tryLocation;
            // Record which handler matches.
            for (const auto &h : scope.handlers) {
              if (h.isCatchAll || isSubtypeOf(exceptionType, h.caughtType)) {
                pi.caughtBy = h.isCatchAll ? "..." : h.caughtType;
                break;
              }
            }
          }
        }

        // Collect guards on this edge.
        for (const auto &guard : ctx.enclosingGuards)
          pi.guardsOnPath.push_back(guard);

        // Check noexcept barrier.
        if (ctx.callerNoexcept == NoexceptSpec::Noexcept && !pi.isCaught) {
          // A noexcept function would std::terminate, not propagate.
          pi.isCaught = false; // Still not "caught" — it terminates.
        }

        break; // Use the first matching context.
      }
    }

    if (pi.isCaught)
      ++caughtCount;
    else
      ++uncaughtCount;

    result.paths.push_back(std::move(pi));
  }

  if (uncaughtCount == 0)
    result.protection = Protection::AlwaysCaught;
  else if (caughtCount == 0)
    result.protection = Protection::NeverCaught;
  else
    result.protection = Protection::SometimesCaught;

  result.summary = buildSummary(result, functionName, exceptionType);
  return result;
}

// ============================================================================
// Q3: queryAllPathContexts
// ============================================================================

std::vector<PathInfo> ControlFlowOracle::queryAllPathContexts(
    const std::string &functionName,
    const std::vector<std::string> &entryPoints, unsigned maxPaths) const {

  std::vector<std::vector<std::string>> rawPaths;
  findPathsToTarget(functionName, entryPoints, maxPaths, rawPaths);

  std::vector<PathInfo> result;
  for (const auto &chain : rawPaths) {
    PathInfo pi;
    pi.callChain = chain;

    for (size_t i = 0; i + 1 < chain.size(); ++i) {
      auto contexts = cfIndex_.contextsForCaller(chain[i]);
      for (const auto &ctx : contexts) {
        if (ctx.calleeName != chain[i + 1])
          continue;
        for (const auto &scope : ctx.enclosingTryCatches)
          pi.tryCatchesOnPath.push_back(scope);
        for (const auto &guard : ctx.enclosingGuards)
          pi.guardsOnPath.push_back(guard);
        break;
      }
    }
    result.push_back(std::move(pi));
  }
  return result;
}

// ============================================================================
// Q4: queryThrowPropagation (delegates to queryExceptionProtection)
// ============================================================================

ExceptionPathResult ControlFlowOracle::queryThrowPropagation(
    const std::string &throwingFunction, const std::string &thrownType,
    const std::vector<std::string> &entryPoints) const {
  return queryExceptionProtection(throwingFunction, thrownType, entryPoints);
}

// ============================================================================
// Q5: queryNearestCatches
// ============================================================================

std::vector<NearestCatchInfo>
ControlFlowOracle::queryNearestCatches(const std::string &functionName) const {
  std::vector<NearestCatchInfo> result;

  // Walk backward from functionName, looking for the nearest try/catch
  // on each unique caller chain.
  struct SearchState {
    std::string func;
    std::vector<std::string> pathSoFar;
    unsigned depth;
  };

  std::vector<SearchState> queue;
  queue.push_back({functionName, {functionName}, 0});
  std::set<std::string> visited;
  visited.insert(functionName);

  while (!queue.empty()) {
    auto state = std::move(queue.back());
    queue.pop_back();

    // Cap search depth.
    if (state.depth > 20)
      continue;

    auto callers = graph_.callersOf(state.func);
    for (const auto &edge : callers) {
      if (visited.count(edge.callerName))
        continue;

      // Check if the call site has a try/catch.
      auto contexts = cfIndex_.contextsForCaller(edge.callerName);
      bool foundCatch = false;
      for (const auto &ctx : contexts) {
        if (ctx.calleeName != state.func)
          continue;
        if (!ctx.enclosingTryCatches.empty()) {
          NearestCatchInfo nci;
          nci.pathSegment = state.pathSoFar;
          nci.pathSegment.insert(nci.pathSegment.begin(), edge.callerName);
          nci.scope = ctx.enclosingTryCatches.front(); // Innermost.
          nci.framesFromTarget = state.depth + 1;
          result.push_back(std::move(nci));
          foundCatch = true;
        }
        break;
      }

      if (!foundCatch) {
        visited.insert(edge.callerName);
        auto newPath = state.pathSoFar;
        newPath.insert(newPath.begin(), edge.callerName);
        queue.push_back({edge.callerName, std::move(newPath),
                         state.depth + 1});
      }
    }
  }

  return result;
}

// ============================================================================
// Summary generation
// ============================================================================

std::string
ControlFlowOracle::buildSummary(const ExceptionPathResult &result,
                                const std::string &functionName,
                                const std::string &exceptionType) {
  std::ostringstream ss;

  switch (result.protection) {
  case Protection::AlwaysCaught:
    ss << functionName << " throwing " << exceptionType
       << " is caught on all " << result.paths.size()
       << " path(s) from entry points.";
    if (!result.paths.empty() && !result.paths[0].caughtBy.empty()) {
      ss << " Caught by " << result.paths[0].caughtBy
         << " at " << result.paths[0].caughtAt << ".";
    }
    break;

  case Protection::NeverCaught:
    ss << functionName << " throwing " << exceptionType
       << " is NOT caught on any of the " << result.paths.size()
       << " path(s) from entry points.";
    break;

  case Protection::SometimesCaught: {
    unsigned caught = 0, uncaught = 0;
    for (const auto &p : result.paths) {
      if (p.isCaught)
        ++caught;
      else
        ++uncaught;
    }
    ss << functionName << " throwing " << exceptionType << " is caught on "
       << caught << " of " << result.paths.size() << " path(s).";
    // Show the first uncaught path.
    for (const auto &p : result.paths) {
      if (!p.isCaught) {
        ss << " Uncaught path: ";
        for (size_t i = 0; i < p.callChain.size(); ++i) {
          if (i > 0)
            ss << " -> ";
          ss << p.callChain[i];
        }
        ss << ".";
        break;
      }
    }
    break;
  }

  case Protection::NoexceptBarrier:
    ss << functionName << " is called through a noexcept function, "
       << "which would cause std::terminate rather than propagation.";
    break;

  case Protection::Unknown:
    ss << "No call paths found from entry points to " << functionName << ".";
    break;
  }

  return ss.str();
}

} // namespace vycor

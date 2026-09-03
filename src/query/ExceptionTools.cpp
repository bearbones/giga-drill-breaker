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


#include "vycor/query/Tools.h"
#include "vycor/query/Identity.h"
#include "vycor/query/Serialize.h"
#include "Registry.h"
#include "Schema.h"

#include "llvm/ADT/StringRef.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include "vycor/ext/Extensions.h"

namespace vycor {

// ============================================================================
// Tool 5: query_exception_safety
// ============================================================================

static const char *protectionToStr(Protection p) {
  switch (p) {
  case Protection::AlwaysCaught: return "always_caught";
  case Protection::SometimesCaught: return "sometimes_caught";
  case Protection::NeverCaught: return "never_caught";
  case Protection::NoexceptBarrier: return "noexcept_barrier";
  case Protection::Unknown: return "unknown";
  }
  return "unknown";
}

static llvm::json::Value
handleQueryExceptionSafety(const llvm::json::Object &args,
                           const ToolContext &ctx) {
  std::optional<llvm::json::Value> ambiguous;
  auto ident = resolveIdentity(args, ctx, "function", "usr", ambiguous);
  if (ambiguous)
    return std::move(*ambiguous);
  if (!ident)
    return errorResult("Missing required parameter 'function' (or 'usr')");

  std::string exceptionType;
  if (auto et = args.getString("exception_type"))
    exceptionType = et->str();

  std::vector<std::string> entryPoints;
  if (auto *epsArr = args.getArray("entry_points")) {
    for (auto &v : *epsArr) {
      if (auto s = v.getAsString())
        entryPoints.push_back(s->str());
    }
  }
  if (entryPoints.empty())
    entryPoints = ctx.entryPoints;

  auto result =
      ctx.oracle.queryExceptionProtection(*ident, exceptionType, entryPoints);

  llvm::json::Object obj;
  auto function = args.getString("function");
  obj["function"] = function ? function->str() : *ident;
  attachUsr(obj, ctx, *ident);
  obj["protection"] = protectionToStr(result.protection);
  obj["totalPaths"] = static_cast<int64_t>(result.paths.size());
  obj["summary"] = result.summary;

  // Include path summaries (without full detail to keep response manageable).
  int64_t caught = 0, uncaught = 0;
  for (auto &p : result.paths) {
    if (p.isCaught)
      ++caught;
    else
      ++uncaught;
  }
  obj["caughtPaths"] = caught;
  obj["uncaughtPaths"] = uncaught;

  return llvm::json::Value(std::move(obj));
}

// ============================================================================
// Tool 6: query_call_site_context
// ============================================================================

// Non-error disambiguation response for a call-site spelling shared by
// several live contexts (macro expansion): the client picks a caller and
// re-queries with the `caller` parameter. Candidates sorted by callerUsr
// for determinism.
static llvm::json::Value
makeAmbiguousSiteResult(llvm::StringRef callSite,
                        std::vector<CallSiteContext> contexts) {
  std::sort(contexts.begin(), contexts.end(),
            [](const CallSiteContext &a, const CallSiteContext &b) {
              return a.callerUsr < b.callerUsr;
            });
  llvm::json::Array candidates;
  for (const auto &c : contexts) {
    llvm::json::Object cand;
    cand["callSite"] = c.callSite;
    cand["callerUsr"] = c.callerUsr;
    cand["callerName"] = c.callerName;
    candidates.push_back(llvm::json::Value(std::move(cand)));
  }
  llvm::json::Object obj;
  obj["ambiguous"] = true;
  obj["parameter"] = "caller";
  obj["callSite"] = callSite.str();
  obj["candidates"] = std::move(candidates);
  obj["note"] = "Multiple call sites share this spelling (macro expansion). "
                "Re-run with the 'caller' parameter (USR or qualified name) "
                "of the intended enclosing function.";
  return llvm::json::Value(std::move(obj));
}

// Resolves a call-site spelling to ONE live context per the disambiguation
// contract: `caller` provided -> the precise (spelling, caller) compound
// lookup; absent -> the unique live context, or `ambiguous` set to the
// candidates response when several contexts share the spelling. Returns
// nullopt with `ambiguous` unset when nothing is indexed (the handler emits
// its not-indexed error).
static std::optional<CallSiteContext>
resolveCallSiteContext(const llvm::json::Object &args,
                       const ToolContext &ctx, const std::string &callSite,
                       std::optional<llvm::json::Value> &ambiguous) {
  if (auto caller = args.getString("caller"))
    return ctx.cfIndex.contextAtSite(callSite, caller->str());
  auto contexts = ctx.cfIndex.contextsAtSite(callSite);
  if (contexts.empty())
    return std::nullopt;
  if (contexts.size() >= 2) {
    ambiguous = makeAmbiguousSiteResult(callSite, std::move(contexts));
    return std::nullopt;
  }
  return std::move(contexts.front());
}

static llvm::json::Value
handleQueryCallSiteContext(const llvm::json::Object &args,
                          const ToolContext &ctx) {
  auto callSite = args.getString("call_site");
  if (!callSite)
    return errorResult("Missing required parameter 'call_site'");

  // Validate file:line:col format. Split on the rightmost two colons so that
  // Unix absolute paths are preserved.
  auto raw = callSite->str();
  auto lastColon = raw.rfind(':');
  auto secondLast =
      lastColon == std::string::npos ? std::string::npos
                                      : raw.rfind(':', lastColon - 1);
  auto isDigits = [](llvm::StringRef s) {
    if (s.empty())
      return false;
    for (char c : s)
      if (!std::isdigit(static_cast<unsigned char>(c)))
        return false;
    return true;
  };
  if (lastColon == std::string::npos || secondLast == std::string::npos ||
      secondLast == 0 ||
      !isDigits(llvm::StringRef(raw).substr(secondLast + 1,
                                            lastColon - secondLast - 1)) ||
      !isDigits(llvm::StringRef(raw).substr(lastColon + 1))) {
    return errorResult(
        "Invalid call_site format: expected 'file:line:col' (e.g. "
        "'src/foo.cpp:12:3'), got '" +
        raw + "'");
  }

  // Distinguish "not indexed" from "indexed with no enclosing try". When
  // several contexts share the spelling (macro expansion) and no `caller`
  // narrows them, a non-error candidates response is returned instead.
  std::optional<llvm::json::Value> ambiguous;
  const auto rawCtx = resolveCallSiteContext(args, ctx, raw, ambiguous);
  if (ambiguous)
    return std::move(*ambiguous);
  if (!rawCtx) {
    return errorResult(
        "Call site not indexed: '" + raw +
        "'. Ensure the path matches the compilation database "
        "canonicalization (typically an absolute path).");
  }

  // Built from the resolved context directly (the same fields
  // ControlFlowOracle::queryCallSite derives, but honoring the specific
  // caller-qualified context rather than the first spelling match).
  llvm::json::Object obj;
  obj["callSite"] = rawCtx->callSite;
  obj["caller"] = rawCtx->callerName;
  obj["callerUsr"] = rawCtx->callerUsr;
  obj["callee"] = rawCtx->calleeName;
  obj["isUnderTryCatch"] = !rawCtx->enclosingTryCatches.empty();
  obj["wouldTerminateIfThrows"] =
      (rawCtx->callerNoexcept == NoexceptSpec::Noexcept);
  obj["enclosingScopeCount"] =
      static_cast<int64_t>(rawCtx->enclosingTryCatches.size());
  obj["enclosingGuardCount"] =
      static_cast<int64_t>(rawCtx->enclosingGuards.size());
  obj["liveRaiiLocalsCount"] =
      static_cast<int64_t>(rawCtx->liveRaiiLocals.size());

  // Include scope details.
  llvm::json::Array scopes;
  for (auto &scope : rawCtx->enclosingTryCatches) {
    llvm::json::Object s;
    s["tryLocation"] = scope.tryLocation;
    s["enclosingFunction"] = scope.enclosingFunction;
    s["nestingDepth"] = static_cast<int64_t>(scope.nestingDepth);
    llvm::json::Array handlers;
    for (auto &h : scope.handlers) {
      llvm::json::Object ho;
      ho["caughtType"] = h.caughtType;
      ho["isCatchAll"] = h.isCatchAll;
      ho["body"] = h.bodySummary;
      handlers.push_back(llvm::json::Value(std::move(ho)));
    }
    s["handlers"] = std::move(handlers);
    scopes.push_back(llvm::json::Value(std::move(s)));
  }
  obj["enclosingScopes"] = std::move(scopes);

  // Guard details (innermost first), including any organization annotation
  // (feature flags etc.) — previously only the count was reported.
  llvm::json::Array guards;
  for (auto &g : rawCtx->enclosingGuards)
    guards.push_back(serializeGuard(g));
  obj["enclosingGuards"] = std::move(guards);

  return llvm::json::Value(std::move(obj));
}

// ============================================================================
// Tool 6a: query_raii_scopes_at_callsite
// ============================================================================

static const char *raiiKindToString(RaiiKind k) {
  switch (k) {
  case RaiiKind::Lock: return "lock";
  case RaiiKind::SmartPtr: return "smart_ptr";
  case RaiiKind::Other: return "other";
  }
  return "other";
}

static std::optional<RaiiKind> parseRaiiKind(llvm::StringRef s) {
  if (s == "lock") return RaiiKind::Lock;
  if (s == "smart_ptr") return RaiiKind::SmartPtr;
  if (s == "other") return RaiiKind::Other;
  return std::nullopt;
}

static llvm::json::Value
handleQueryRaiiScopesAtCallsite(const llvm::json::Object &args,
                                const ToolContext &ctx) {
  auto callSite = args.getString("call_site");
  if (!callSite)
    return errorResult("Missing required parameter 'call_site'");

  // Optional kinds filter. If absent or empty, all kinds are included.
  std::set<RaiiKind> allowed;
  if (auto *kindsArr = args.getArray("kinds")) {
    for (auto &v : *kindsArr) {
      if (auto s = v.getAsString()) {
        auto k = parseRaiiKind(*s);
        if (!k) {
          return errorResult(
              "Invalid value in kinds: '" + s->str() +
              "' (expected lock, smart_ptr, or other)");
        }
        allowed.insert(*k);
      }
    }
  }
  bool filterByKind = !allowed.empty();

  // Same disambiguation contract as query_call_site_context: an optional
  // `caller` routes to the precise compound-key lookup; a bare spelling
  // matching several live contexts returns a candidates response.
  std::optional<llvm::json::Value> ambiguous;
  const auto csCtx =
      resolveCallSiteContext(args, ctx, callSite->str(), ambiguous);
  if (ambiguous)
    return std::move(*ambiguous);
  if (!csCtx) {
    return errorResult(
        "Call site not indexed: '" + callSite->str() +
        "'. Ensure the path matches the compilation database "
        "canonicalization (typically an absolute path).");
  }

  llvm::json::Array locals;
  for (const auto &l : csCtx->liveRaiiLocals) {
    if (filterByKind && !allowed.count(l.kind))
      continue;
    llvm::json::Object obj;
    obj["typeName"] = l.typeName;
    obj["varName"] = l.varName;
    obj["declLocation"] = l.declLocation;
    obj["kind"] = raiiKindToString(l.kind);
    locals.push_back(llvm::json::Value(std::move(obj)));
  }

  llvm::json::Object out;
  out["callSite"] = callSite->str();
  out["caller"] = csCtx->callerName;
  out["callerUsr"] = csCtx->callerUsr;
  out["callee"] = csCtx->calleeName;
  out["locals"] = std::move(locals);
  return llvm::json::Value(std::move(out));
}

void registerExceptionTools(std::vector<ToolEntry> &tools) {
  // 5. query_exception_safety
  {
    llvm::json::Object props;
    props["function"] = stringProp(
        "Target function qualified name. Provide 'function' or 'usr' (usr "
        "wins when both are present).");
    props["usr"] = stringProp(
        "Exact USR of the target function. Bypasses name resolution — use "
        "it to pick one overload/specialization when the name is "
        "ambiguous.");
    addIdentityRefinementProps(props, "");
    props["exception_type"] = stringProp(
        "Exception type to check (e.g. 'std::runtime_error')");
    props["entry_points"] = stringArrayProp(
        "Entry point function names (default: configured entry points)");
    llvm::json::Object schema;
    schema["type"] = "object";
    schema["properties"] = std::move(props);

    tools.push_back({"query_exception_safety",
                     "Determine whether a function is protected by try/catch "
                     "on its call paths from entry points. Reports always, "
                     "sometimes, or never caught. An ambiguous name returns "
                     "{ambiguous:true, candidates:[...]} — re-query with "
                     "'usr'.",
                     llvm::json::Value(std::move(schema)),
                     handleQueryExceptionSafety});
  }

  // 6. query_call_site_context
  {
    llvm::json::Object props;
    props["call_site"] = stringProp(
        "Call site location formatted as 'file:line:col'. The file path "
        "must match the compilation database canonicalization (typically "
        "an absolute path). Returns isError if the site is not indexed.");
    props["caller"] = stringProp(
        "Optional enclosing function (qualified name or USR). Needed when "
        "several call sites share one spelling (a macro expanded in "
        "different functions); without it such a spelling returns "
        "{ambiguous:true, candidates:[...]} listing the callers.");
    llvm::json::Array req;
    req.push_back("call_site");
    llvm::json::Object schema;
    schema["type"] = "object";
    schema["properties"] = std::move(props);
    schema["required"] = std::move(req);

    tools.push_back({"query_call_site_context",
                     "Get exception handling and guard context at a specific "
                     "call site location (file:line:col). Shows enclosing "
                     "try/catch scopes and conditional guards. A spelling "
                     "shared by several call sites (macro expansion) returns "
                     "{ambiguous:true, candidates:[...]} — re-query with "
                     "'caller'.",
                     llvm::json::Value(std::move(schema)),
                     handleQueryCallSiteContext});
  }

  // 6a. query_raii_scopes_at_callsite
  {
    llvm::json::Object props;
    props["call_site"] = stringProp(
        "Call site location formatted as 'file:line:col'. Must match the "
        "compilation database canonicalization.");
    props["caller"] = stringProp(
        "Optional enclosing function (qualified name or USR). Needed when "
        "several call sites share one spelling (a macro expanded in "
        "different functions); without it such a spelling returns "
        "{ambiguous:true, candidates:[...]} listing the callers.");
    props["kinds"] = stringArrayProp(
        "Filter by kind: lock, smart_ptr, other. Default: all kinds.");
    llvm::json::Array req;
    req.push_back("call_site");
    llvm::json::Object schema;
    schema["type"] = "object";
    schema["properties"] = std::move(props);
    schema["required"] = std::move(req);

    tools.push_back({"query_raii_scopes_at_callsite",
                     "List RAII-capable locals (non-trivial-destructor) live "
                     "at a call site. Each entry is {typeName, varName, "
                     "kind, declLocation}, kind in {lock, smart_ptr, other}. "
                     "Use `kinds` to narrow the response (e.g. [\"lock\"] "
                     "for concurrency audits). A spelling shared by several "
                     "call sites (macro expansion) returns {ambiguous:true, "
                     "candidates:[...]} — re-query with 'caller'.",
                     llvm::json::Value(std::move(schema)),
                     handleQueryRaiiScopesAtCallsite});
  }
}

} // namespace vycor

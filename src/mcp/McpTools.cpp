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

#include "giga_drill/mcp/McpTools.h"
#include "giga_drill/mugann/DeadCodeAnalyzer.h"

#include <algorithm>
#include <functional>
#include <set>
#include <string>
#include <vector>

namespace giga_drill {

// ============================================================================
// JSON helper: build an MCP tool result with text content
// ============================================================================

static llvm::json::Value makeTextResult(llvm::json::Value payload) {
  std::string text;
  llvm::raw_string_ostream os(text);
  os << payload;
  os.flush();

  llvm::json::Object content;
  content["type"] = "text";
  content["text"] = std::move(text);

  llvm::json::Array contentArr;
  contentArr.push_back(llvm::json::Value(std::move(content)));

  llvm::json::Object result;
  result["content"] = std::move(contentArr);
  return llvm::json::Value(std::move(result));
}

static llvm::json::Value makeErrorResult(llvm::StringRef message) {
  llvm::json::Object content;
  content["type"] = "text";
  content["text"] = message.str();

  llvm::json::Array contentArr;
  contentArr.push_back(llvm::json::Value(std::move(content)));

  llvm::json::Object result;
  result["content"] = std::move(contentArr);
  result["isError"] = true;
  return llvm::json::Value(std::move(result));
}

// ============================================================================
// Enum serialization helpers
// ============================================================================

static const char *edgeKindToString(EdgeKind k) {
  switch (k) {
  case EdgeKind::DirectCall:
    return "DirectCall";
  case EdgeKind::VirtualDispatch:
    return "VirtualDispatch";
  case EdgeKind::FunctionPointer:
    return "FunctionPointer";
  case EdgeKind::ConstructorCall:
    return "ConstructorCall";
  case EdgeKind::DestructorCall:
    return "DestructorCall";
  case EdgeKind::OperatorCall:
    return "OperatorCall";
  case EdgeKind::TemplateInstantiation:
    return "TemplateInstantiation";
  }
  return "Unknown";
}

static const char *confidenceToString(Confidence c) {
  switch (c) {
  case Confidence::Proven:
    return "Proven";
  case Confidence::Plausible:
    return "Plausible";
  case Confidence::Unknown:
    return "Unknown";
  }
  return "Unknown";
}

static EdgeKind parseEdgeKind(llvm::StringRef s) {
  if (s == "DirectCall") return EdgeKind::DirectCall;
  if (s == "VirtualDispatch") return EdgeKind::VirtualDispatch;
  if (s == "FunctionPointer") return EdgeKind::FunctionPointer;
  if (s == "ConstructorCall") return EdgeKind::ConstructorCall;
  if (s == "DestructorCall") return EdgeKind::DestructorCall;
  if (s == "OperatorCall") return EdgeKind::OperatorCall;
  if (s == "TemplateInstantiation") return EdgeKind::TemplateInstantiation;
  return EdgeKind::DirectCall; // fallback
}

static Confidence parseConfidence(llvm::StringRef s) {
  if (s == "Proven") return Confidence::Proven;
  if (s == "Plausible") return Confidence::Plausible;
  return Confidence::Unknown;
}

static int confidenceRank(Confidence c) {
  switch (c) {
  case Confidence::Proven: return 2;
  case Confidence::Plausible: return 1;
  case Confidence::Unknown: return 0;
  }
  return 0;
}

// ============================================================================
// Serialize a CallGraphEdge to JSON
// ============================================================================

static llvm::json::Value edgeToJson(const CallGraphEdge &e) {
  llvm::json::Object obj;
  obj["callerName"] = e.callerName;
  obj["calleeName"] = e.calleeName;
  obj["kind"] = edgeKindToString(e.kind);
  obj["confidence"] = confidenceToString(e.confidence);
  obj["callSite"] = e.callSite;
  if (e.indirectionDepth > 0)
    obj["indirectionDepth"] = static_cast<int64_t>(e.indirectionDepth);
  return llvm::json::Value(std::move(obj));
}

// ============================================================================
// Tool 1: lookup_function
// ============================================================================

static llvm::json::Value handleLookupFunction(const llvm::json::Object &args,
                                              const McpToolContext &ctx) {
  auto name = args.getString("name");
  if (!name)
    return makeErrorResult("Missing required parameter 'name'");

  auto *node = ctx.graph.findNode(name->str());
  if (!node)
    return makeErrorResult("Function not found: " + name->str());

  llvm::json::Object obj;
  obj["qualifiedName"] = node->qualifiedName;
  obj["file"] = node->file;
  obj["line"] = static_cast<int64_t>(node->line);
  obj["isEntryPoint"] = node->isEntryPoint;
  obj["isVirtual"] = node->isVirtual;
  if (!node->enclosingClass.empty())
    obj["enclosingClass"] = node->enclosingClass;
  return makeTextResult(llvm::json::Value(std::move(obj)));
}

// ============================================================================
// Tool 2: get_callees
// ============================================================================

static llvm::json::Value handleGetCallees(const llvm::json::Object &args,
                                          const McpToolContext &ctx) {
  auto name = args.getString("name");
  if (!name)
    return makeErrorResult("Missing required parameter 'name'");

  // Optional filters.
  std::set<EdgeKind> kindFilter;
  if (auto *kindsArr = args.getArray("edge_kinds")) {
    for (auto &v : *kindsArr) {
      if (auto s = v.getAsString())
        kindFilter.insert(parseEdgeKind(*s));
    }
  }

  Confidence minConf = Confidence::Unknown;
  if (auto mc = args.getString("min_confidence"))
    minConf = parseConfidence(*mc);

  auto edges = ctx.graph.calleesOf(name->str());
  llvm::json::Array results;
  for (auto *e : edges) {
    if (!kindFilter.empty() && !kindFilter.count(e->kind))
      continue;
    if (confidenceRank(e->confidence) < confidenceRank(minConf))
      continue;
    results.push_back(edgeToJson(*e));
  }

  llvm::json::Object obj;
  obj["function"] = name->str();
  obj["calleeCount"] = static_cast<int64_t>(results.size());
  obj["callees"] = std::move(results);
  return makeTextResult(llvm::json::Value(std::move(obj)));
}

// ============================================================================
// Tool 3: get_callers
// ============================================================================

static llvm::json::Value handleGetCallers(const llvm::json::Object &args,
                                          const McpToolContext &ctx) {
  auto name = args.getString("name");
  if (!name)
    return makeErrorResult("Missing required parameter 'name'");

  std::set<EdgeKind> kindFilter;
  if (auto *kindsArr = args.getArray("edge_kinds")) {
    for (auto &v : *kindsArr) {
      if (auto s = v.getAsString())
        kindFilter.insert(parseEdgeKind(*s));
    }
  }

  Confidence minConf = Confidence::Unknown;
  if (auto mc = args.getString("min_confidence"))
    minConf = parseConfidence(*mc);

  auto edges = ctx.graph.callersOf(name->str());
  llvm::json::Array results;
  for (auto *e : edges) {
    if (!kindFilter.empty() && !kindFilter.count(e->kind))
      continue;
    if (confidenceRank(e->confidence) < confidenceRank(minConf))
      continue;
    results.push_back(edgeToJson(*e));
  }

  llvm::json::Object obj;
  obj["function"] = name->str();
  obj["callerCount"] = static_cast<int64_t>(results.size());
  obj["callers"] = std::move(results);
  return makeTextResult(llvm::json::Value(std::move(obj)));
}

// ============================================================================
// Tool 4: find_call_chain
// ============================================================================

static llvm::json::Value handleFindCallChain(const llvm::json::Object &args,
                                             const McpToolContext &ctx) {
  auto to = args.getString("to");
  if (!to)
    return makeErrorResult("Missing required parameter 'to'");

  int64_t maxPaths = 10;
  if (auto mp = args.getInteger("max_paths"))
    maxPaths = *mp;

  int64_t maxDepth = 20;
  if (auto md = args.getInteger("max_depth"))
    maxDepth = *md;

  // Determine start nodes.
  std::vector<std::string> starts;
  if (auto from = args.getString("from")) {
    starts.push_back(from->str());
  } else {
    starts = ctx.entryPoints;
  }

  // Reverse DFS from target to start nodes (same algorithm as
  // ControlFlowOracle::findPathsToTarget).
  std::set<std::string> startSet(starts.begin(), starts.end());
  std::vector<std::vector<std::string>> foundPaths;
  std::vector<std::string> currentPath;
  std::set<std::string> onPath;

  std::function<void(const std::string &, unsigned)> dfs =
      [&](const std::string &node, unsigned depth) {
        if (static_cast<int64_t>(foundPaths.size()) >= maxPaths)
          return;
        if (depth > static_cast<unsigned>(maxDepth))
          return;

        currentPath.push_back(node);
        onPath.insert(node);

        if (startSet.count(node)) {
          // Found a path — store reversed (start -> ... -> target).
          std::vector<std::string> path(currentPath.rbegin(),
                                        currentPath.rend());
          foundPaths.push_back(std::move(path));
        } else {
          auto callers = ctx.graph.callersOf(node);
          for (auto *edge : callers) {
            if (onPath.count(edge->callerName))
              continue;
            dfs(edge->callerName, depth + 1);
            if (static_cast<int64_t>(foundPaths.size()) >= maxPaths)
              break;
          }
        }

        currentPath.pop_back();
        onPath.erase(node);
      };

  dfs(to->str(), 0);

  llvm::json::Array pathsJson;
  for (auto &path : foundPaths) {
    llvm::json::Array chain;
    for (auto &fn : path)
      chain.push_back(fn);
    pathsJson.push_back(llvm::json::Value(std::move(chain)));
  }

  llvm::json::Object obj;
  obj["target"] = to->str();
  obj["pathCount"] = static_cast<int64_t>(foundPaths.size());
  obj["paths"] = std::move(pathsJson);
  return makeTextResult(llvm::json::Value(std::move(obj)));
}

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
                           const McpToolContext &ctx) {
  auto function = args.getString("function");
  if (!function)
    return makeErrorResult("Missing required parameter 'function'");

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

  auto result = ctx.oracle.queryExceptionProtection(function->str(),
                                                    exceptionType, entryPoints);

  llvm::json::Object obj;
  obj["function"] = function->str();
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

  return makeTextResult(llvm::json::Value(std::move(obj)));
}

// ============================================================================
// Tool 6: query_call_site_context
// ============================================================================

static llvm::json::Value
handleQueryCallSiteContext(const llvm::json::Object &args,
                          const McpToolContext &ctx) {
  auto callSite = args.getString("call_site");
  if (!callSite)
    return makeErrorResult("Missing required parameter 'call_site'");

  auto info = ctx.oracle.queryCallSite(callSite->str());

  llvm::json::Object obj;
  obj["callSite"] = info.callSite;
  obj["caller"] = info.caller;
  obj["callee"] = info.callee;
  obj["isUnderTryCatch"] = info.isUnderTryCatch;
  obj["wouldTerminateIfThrows"] = info.wouldTerminateIfThrows;
  obj["enclosingScopeCount"] =
      static_cast<int64_t>(info.enclosingScopes.size());
  obj["enclosingGuardCount"] =
      static_cast<int64_t>(info.enclosingGuards.size());

  // Include scope details.
  llvm::json::Array scopes;
  for (auto &scope : info.enclosingScopes) {
    llvm::json::Object s;
    s["tryLocation"] = scope.tryLocation;
    s["enclosingFunction"] = scope.enclosingFunction;
    s["nestingDepth"] = static_cast<int64_t>(scope.nestingDepth);
    llvm::json::Array handlers;
    for (auto &h : scope.handlers) {
      llvm::json::Object ho;
      ho["caughtType"] = h.caughtType;
      ho["isCatchAll"] = h.isCatchAll;
      handlers.push_back(llvm::json::Value(std::move(ho)));
    }
    s["handlers"] = std::move(handlers);
    scopes.push_back(llvm::json::Value(std::move(s)));
  }
  obj["enclosingScopes"] = std::move(scopes);

  return makeTextResult(llvm::json::Value(std::move(obj)));
}

// ============================================================================
// Tool 7: analyze_dead_code
// ============================================================================

static const char *livenessToStr(Liveness l) {
  switch (l) {
  case Liveness::Alive: return "alive";
  case Liveness::OptimisticallyAlive: return "optimistically_alive";
  case Liveness::Dead: return "dead";
  }
  return "unknown";
}

static llvm::json::Value handleAnalyzeDeadCode(const llvm::json::Object &args,
                                               const McpToolContext &ctx) {
  std::vector<std::string> entryPoints;
  if (auto *epsArr = args.getArray("entry_points")) {
    for (auto &v : *epsArr) {
      if (auto s = v.getAsString())
        entryPoints.push_back(s->str());
    }
  }
  if (entryPoints.empty())
    entryPoints = ctx.entryPoints;

  bool includeOptimistic = true;
  if (auto io = args.getBoolean("include_optimistic"))
    includeOptimistic = *io;

  DeadCodeAnalyzer analyzer(ctx.graph, entryPoints);
  analyzer.analyzePessimistic();
  if (includeOptimistic)
    analyzer.analyzeOptimistic();

  auto results = analyzer.getResults();

  // Group by liveness.
  llvm::json::Array alive, optimistic, dead;
  for (auto &kv : results) {
    auto *node = ctx.graph.findNode(kv.first);
    llvm::json::Object entry;
    entry["name"] = kv.first;
    if (node) {
      entry["file"] = node->file;
      entry["line"] = static_cast<int64_t>(node->line);
    }

    switch (kv.second) {
    case Liveness::Alive:
      alive.push_back(llvm::json::Value(std::move(entry)));
      break;
    case Liveness::OptimisticallyAlive:
      optimistic.push_back(llvm::json::Value(std::move(entry)));
      break;
    case Liveness::Dead:
      dead.push_back(llvm::json::Value(std::move(entry)));
      break;
    }
  }

  llvm::json::Object obj;
  obj["totalFunctions"] = static_cast<int64_t>(results.size());
  obj["aliveCount"] = static_cast<int64_t>(alive.size());
  obj["optimisticallyAliveCount"] = static_cast<int64_t>(optimistic.size());
  obj["deadCount"] = static_cast<int64_t>(dead.size());
  obj["dead"] = std::move(dead);
  obj["optimisticallyAlive"] = std::move(optimistic);
  // Omit alive list to keep response size down — caller usually wants dead.
  return makeTextResult(llvm::json::Value(std::move(obj)));
}

// ============================================================================
// Tool 8: get_class_hierarchy
// ============================================================================

static llvm::json::Value
handleGetClassHierarchy(const llvm::json::Object &args,
                        const McpToolContext &ctx) {
  auto className = args.getString("class_name");
  if (!className)
    return makeErrorResult("Missing required parameter 'class_name'");

  bool transitive = false;
  if (auto t = args.getBoolean("include_transitive"))
    transitive = *t;

  bool includeOverrides = false;
  if (auto o = args.getBoolean("include_overrides"))
    includeOverrides = *o;

  auto derived = transitive ? ctx.graph.getAllDerivedClasses(className->str())
                            : ctx.graph.getDerivedClasses(className->str());

  llvm::json::Array derivedArr;
  for (auto &cls : derived)
    derivedArr.push_back(cls);

  llvm::json::Object obj;
  obj["className"] = className->str();
  obj["derivedClassCount"] = static_cast<int64_t>(derived.size());
  obj["derivedClasses"] = std::move(derivedArr);

  if (includeOverrides) {
    // Collect all virtual methods that belong to this class and show overrides.
    llvm::json::Array overridesArr;
    for (auto *node : ctx.graph.allNodes()) {
      if (node->enclosingClass != className->str())
        continue;
      if (!node->isVirtual)
        continue;
      auto overrides = ctx.graph.getOverrides(node->qualifiedName);
      if (overrides.empty())
        continue;
      llvm::json::Object methodObj;
      methodObj["baseMethod"] = node->qualifiedName;
      llvm::json::Array ovArr;
      for (auto &ov : overrides)
        ovArr.push_back(ov);
      methodObj["overrides"] = std::move(ovArr);
      overridesArr.push_back(llvm::json::Value(std::move(methodObj)));
    }
    obj["virtualMethodOverrides"] = std::move(overridesArr);
  }

  return makeTextResult(llvm::json::Value(std::move(obj)));
}

// ============================================================================
// JSON Schema builders for tool input schemas
// ============================================================================

static llvm::json::Value stringProp(llvm::StringRef desc) {
  llvm::json::Object p;
  p["type"] = "string";
  p["description"] = desc.str();
  return llvm::json::Value(std::move(p));
}

static llvm::json::Value intProp(llvm::StringRef desc) {
  llvm::json::Object p;
  p["type"] = "integer";
  p["description"] = desc.str();
  return llvm::json::Value(std::move(p));
}

static llvm::json::Value boolProp(llvm::StringRef desc) {
  llvm::json::Object p;
  p["type"] = "boolean";
  p["description"] = desc.str();
  return llvm::json::Value(std::move(p));
}

static llvm::json::Value stringArrayProp(llvm::StringRef desc) {
  llvm::json::Object items;
  items["type"] = "string";
  llvm::json::Object p;
  p["type"] = "array";
  p["items"] = std::move(items);
  p["description"] = desc.str();
  return llvm::json::Value(std::move(p));
}

// ============================================================================
// Tool registration
// ============================================================================

std::vector<McpToolEntry> getRegisteredTools() {
  std::vector<McpToolEntry> tools;

  // 1. lookup_function
  {
    llvm::json::Object props;
    props["name"] = stringProp("Qualified function name (e.g. 'MyClass::process')");
    llvm::json::Array req;
    req.push_back("name");
    llvm::json::Object schema;
    schema["type"] = "object";
    schema["properties"] = std::move(props);
    schema["required"] = std::move(req);

    tools.push_back({"lookup_function",
                     "Look up metadata for a function by qualified name. "
                     "Returns file, line, class membership, and virtual status.",
                     llvm::json::Value(std::move(schema)),
                     handleLookupFunction});
  }

  // 2. get_callees
  {
    llvm::json::Object props;
    props["name"] = stringProp("Qualified name of the caller function");
    props["edge_kinds"] = stringArrayProp(
        "Filter by edge kind: DirectCall, VirtualDispatch, FunctionPointer, "
        "ConstructorCall, DestructorCall, OperatorCall, TemplateInstantiation");
    props["min_confidence"] = stringProp(
        "Minimum confidence: Proven, Plausible, or Unknown (default: Unknown)");
    llvm::json::Array req;
    req.push_back("name");
    llvm::json::Object schema;
    schema["type"] = "object";
    schema["properties"] = std::move(props);
    schema["required"] = std::move(req);

    tools.push_back({"get_callees",
                     "List all functions called by a given function. "
                     "Supports filtering by edge kind and confidence level.",
                     llvm::json::Value(std::move(schema)),
                     handleGetCallees});
  }

  // 3. get_callers
  {
    llvm::json::Object props;
    props["name"] = stringProp("Qualified name of the callee function");
    props["edge_kinds"] = stringArrayProp(
        "Filter by edge kind: DirectCall, VirtualDispatch, etc.");
    props["min_confidence"] = stringProp(
        "Minimum confidence: Proven, Plausible, or Unknown (default: Unknown)");
    llvm::json::Array req;
    req.push_back("name");
    llvm::json::Object schema;
    schema["type"] = "object";
    schema["properties"] = std::move(props);
    schema["required"] = std::move(req);

    tools.push_back({"get_callers",
                     "List all functions that call a given function. "
                     "Supports filtering by edge kind and confidence level.",
                     llvm::json::Value(std::move(schema)),
                     handleGetCallers});
  }

  // 4. find_call_chain
  {
    llvm::json::Object props;
    props["from"] = stringProp(
        "Source function qualified name (omit to use entry points)");
    props["to"] = stringProp("Target function qualified name");
    props["max_paths"] = intProp("Maximum number of paths to return (default: 10)");
    props["max_depth"] = intProp("Maximum chain length (default: 20)");
    llvm::json::Array req;
    req.push_back("to");
    llvm::json::Object schema;
    schema["type"] = "object";
    schema["properties"] = std::move(props);
    schema["required"] = std::move(req);

    tools.push_back({"find_call_chain",
                     "Find call chains from a source function (or entry points) "
                     "to a target function. Returns all discovered paths.",
                     llvm::json::Value(std::move(schema)),
                     handleFindCallChain});
  }

  // 5. query_exception_safety
  {
    llvm::json::Object props;
    props["function"] = stringProp("Target function qualified name");
    props["exception_type"] = stringProp(
        "Exception type to check (e.g. 'std::runtime_error')");
    props["entry_points"] = stringArrayProp(
        "Entry point function names (default: configured entry points)");
    llvm::json::Array req;
    req.push_back("function");
    llvm::json::Object schema;
    schema["type"] = "object";
    schema["properties"] = std::move(props);
    schema["required"] = std::move(req);

    tools.push_back({"query_exception_safety",
                     "Determine whether a function is protected by try/catch "
                     "on its call paths from entry points. Reports always, "
                     "sometimes, or never caught.",
                     llvm::json::Value(std::move(schema)),
                     handleQueryExceptionSafety});
  }

  // 6. query_call_site_context
  {
    llvm::json::Object props;
    props["call_site"] = stringProp("Call site location as file:line:col");
    llvm::json::Array req;
    req.push_back("call_site");
    llvm::json::Object schema;
    schema["type"] = "object";
    schema["properties"] = std::move(props);
    schema["required"] = std::move(req);

    tools.push_back({"query_call_site_context",
                     "Get exception handling and guard context at a specific "
                     "call site location (file:line:col). Shows enclosing "
                     "try/catch scopes and conditional guards.",
                     llvm::json::Value(std::move(schema)),
                     handleQueryCallSiteContext});
  }

  // 7. analyze_dead_code
  {
    llvm::json::Object props;
    props["entry_points"] = stringArrayProp(
        "Entry point function names (default: configured entry points)");
    props["include_optimistic"] = boolProp(
        "Include optimistically-alive functions (default: true)");
    llvm::json::Object schema;
    schema["type"] = "object";
    schema["properties"] = std::move(props);

    tools.push_back({"analyze_dead_code",
                     "Run dead code analysis via call graph reachability. "
                     "Reports dead, optimistically-alive, and alive functions "
                     "from the configured entry points.",
                     llvm::json::Value(std::move(schema)),
                     handleAnalyzeDeadCode});
  }

  // 8. get_class_hierarchy
  {
    llvm::json::Object props;
    props["class_name"] = stringProp("Qualified class name");
    props["include_transitive"] = boolProp(
        "Include all descendants, not just direct (default: false)");
    props["include_overrides"] = boolProp(
        "Include virtual method override info (default: false)");
    llvm::json::Array req;
    req.push_back("class_name");
    llvm::json::Object schema;
    schema["type"] = "object";
    schema["properties"] = std::move(props);
    schema["required"] = std::move(req);

    tools.push_back({"get_class_hierarchy",
                     "Query class inheritance relationships and virtual method "
                     "overrides. Shows derived classes and optionally which "
                     "methods are overridden in each.",
                     llvm::json::Value(std::move(schema)),
                     handleGetClassHierarchy});
  }

  return tools;
}

} // namespace giga_drill

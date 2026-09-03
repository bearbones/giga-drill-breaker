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
#include <map>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace vycor {

// ============================================================================
// Tool 6b: query_locks_held  —  reverse DFS from target to entry points,
// accumulating Lock-kind RAII locals along each discovered path.
// ============================================================================

namespace {

// Hashable key identifying a lock across call sites.
struct LockKey {
  std::string typeName;
  std::string varName;
  bool operator==(const LockKey &o) const {
    return typeName == o.typeName && varName == o.varName;
  }
};

struct LockKeyHash {
  size_t operator()(const LockKey &k) const {
    return std::hash<std::string>{}(k.typeName) ^
           (std::hash<std::string>{}(k.varName) << 1);
  }
};

struct LockOccurrence {
  std::string typeName;
  std::string varName;
  std::string heldAt; // file:line:col of the call site where it was in scope
};

struct PathResult {
  std::string entryPoint;
  std::vector<std::string> path; // entry → ... → target
  std::vector<LockOccurrence> locksHeld;
};

// Safety caps to bound DFS cost on hub functions and cyclic graphs.
constexpr unsigned kDefaultMaxDepth = 20;
constexpr size_t kMaxPaths = 512;
constexpr size_t kDefaultMaxFanIn = 1000;

// Collect Lock-kind RAII locals live at the edge (caller → callee @ callSite),
// reading from the ControlFlowIndex. Appends deduped-by-key occurrences.
static void collectLocksOnEdge(const ControlFlowIndex &cfIndex,
                               const std::string &callSite,
                               std::vector<LockOccurrence> &out,
                               std::unordered_set<LockKey, LockKeyHash> &seen) {
  const auto cs = cfIndex.contextAtSite(callSite);
  if (!cs)
    return;
  for (const auto &l : cs->liveRaiiLocals) {
    if (l.kind != RaiiKind::Lock)
      continue;
    LockKey key{l.typeName, l.varName};
    if (seen.insert(key).second) {
      out.push_back({l.typeName, l.varName, callSite});
    }
  }
}

// Reverse path search from `target` walking caller edges, entirely in
// interned-id space (the string-space version was measured >10 min on a
// 938-TU graph — same exponential class find_call_chain had). Strings are
// resolved only for emitted paths and skipped hubs. Two prunes bound the
// walk (both proven on find_call_chain):
//  - corridor: a forward BFS from the entry set over callee edges records
//    min-edges-from-entry per node; the DFS only expands callers that can
//    still complete an entry->...->target path within maxDepth.
//  - dead-end memo: a node whose ancestry was exhaustively explored from
//    depth d without reaching an entry cannot succeed on a revisit with
//    depth >= d. Failures caused by the per-path edge exclusion or the
//    kMaxPaths early-stop are search-state-dependent and never memoized;
//    hub skips are deterministic per node and stay memoizable.
// The per-path VISITED-EDGE exclusion (not a node-cycle guard) is
// preserved from the original: parallel edges through the same node pair
// at different call sites legitimately yield distinct lock paths.

using SId = StringInterner::Id;

struct EdgeVisitKey {
  SId caller, callee, site;
  bool operator==(const EdgeVisitKey &o) const {
    return caller == o.caller && callee == o.callee && site == o.site;
  }
};
struct EdgeVisitKeyHash {
  size_t operator()(const EdgeVisitKey &k) const {
    uint64_t h = (static_cast<uint64_t>(k.caller) << 32) | k.callee;
    h ^= (h >> 33);
    h *= 0xff51afd7ed558ccdULL;
    h ^= k.site;
    h ^= (h >> 33);
    return static_cast<size_t>(h);
  }
};

namespace {
struct RawLockPath {
  SId entryPoint;
  std::vector<SId> nodes;     // target -> ... -> entry (reversed at emit)
  std::vector<SId> edgeSites; // parallel to edges along nodes
};

struct LockDfsState {
  const CallGraph &graph;
  const std::unordered_set<SId> &entrySet;
  const std::unordered_map<SId, unsigned> &minFromEntry;
  unsigned maxDepth;
  size_t maxFanIn;
  std::optional<SId> indirectId;
  std::vector<SId> path;
  std::vector<SId> edgeSites;
  std::unordered_set<EdgeVisitKey, EdgeVisitKeyHash> visitedEdges;
  std::unordered_map<SId, unsigned> deadAt;
  std::unordered_map<SId, std::vector<CallGraph::EdgeRef>> callersMemo;
  std::map<SId, size_t> skippedHubs;
  std::vector<RawLockPath> out;
  bool truncated = false;
};
} // namespace

static int lockReverseDfs(LockDfsState &st) {
  constexpr int kFound = 1, kBlocked = 2;
  if (st.out.size() >= kMaxPaths) {
    st.truncated = true;
    return kBlocked;
  }

  SId cur = st.path.back();
  if (st.entrySet.count(cur)) {
    RawLockPath rp;
    rp.entryPoint = cur;
    rp.nodes = st.path;
    rp.edgeSites = st.edgeSites;
    st.out.push_back(std::move(rp));
    return kFound;
  }

  if (st.path.size() >= st.maxDepth)
    return 0; // budget failure — memoizable via the depth comparison

  auto dit = st.deadAt.find(cur);
  if (dit != st.deadAt.end() &&
      static_cast<unsigned>(st.path.size()) >= dit->second)
    return 0;

  // Hub cutoff: never refuse to expand the query target itself.
  // Deterministic per node, so it does not poison the dead-end memo.
  if (st.maxFanIn > 0 && st.path.size() > 1) {
    size_t inDeg = st.graph.storedInDegree(cur);
    if (inDeg > st.maxFanIn) {
      st.skippedHubs.emplace(cur, inDeg);
      return 0;
    }
  }

  int flags = 0;
  auto cit = st.callersMemo.find(cur);
  if (cit == st.callersMemo.end())
    cit = st.callersMemo.emplace(cur, st.graph.callerRefsOf(cur)).first;
  const auto &callers = cit->second;
  for (const auto &edge : callers) {
    // Skip indirect edges — they have no stable callee identity for
    // transitive lock inheritance.
    if (st.indirectId && edge.caller == *st.indirectId)
      continue;
    // Corridor prune: the caller must be reachable from an entry with
    // enough budget left to descend back to the target (path holds
    // st.path.size() nodes; pushing the caller and completing via the
    // entry needs minFromEntry more edges).
    auto mit = st.minFromEntry.find(edge.caller);
    if (mit == st.minFromEntry.end() ||
        st.path.size() + 1 + mit->second > st.maxDepth)
      continue;
    EdgeVisitKey key{edge.caller, edge.callee, edge.callSite};
    if (!st.visitedEdges.insert(key).second) {
      flags |= kBlocked; // path-dependent exclusion
      continue;
    }

    st.path.push_back(edge.caller);
    st.edgeSites.push_back(edge.callSite);
    flags |= lockReverseDfs(st);
    st.edgeSites.pop_back();
    st.path.pop_back();
    st.visitedEdges.erase(key);

    if (st.out.size() >= kMaxPaths) {
      st.truncated = true;
      flags |= kBlocked; // exploration truncated, not exhausted
      break;
    }
  }

  if (!(flags & (kFound | kBlocked))) {
    auto [it, inserted] =
        st.deadAt.emplace(cur, static_cast<unsigned>(st.path.size()));
    if (!inserted && static_cast<unsigned>(st.path.size()) < it->second)
      it->second = static_cast<unsigned>(st.path.size());
  }
  return flags;
}

static std::vector<PathResult>
collectLocksHeld(const CallGraph &graph, const ControlFlowIndex &cfIndex,
                 const std::string &target,
                 const std::vector<std::string> &entryPoints,
                 unsigned maxDepth, size_t maxFanIn,
                 std::map<std::string, size_t> &skippedHubs,
                 bool &truncated) {
  std::vector<PathResult> out;
  truncated = false;
  if (entryPoints.empty())
    return out;

  const auto &interner = graph.interner();
  auto targetId = interner.find(target);
  if (!targetId)
    return out;
  std::unordered_set<SId> entrySet;
  for (const auto &ep : entryPoints) {
    if (auto id = interner.find(ep))
      entrySet.insert(*id);
  }
  if (entrySet.empty())
    return out;

  // Corridor BFS: min edges from any entry, over callee edges, bounded by
  // maxDepth (node budget; a path of N nodes has N-1 edges).
  std::unordered_map<SId, unsigned> minFromEntry;
  {
    std::vector<SId> frontier(entrySet.begin(), entrySet.end());
    for (SId e : frontier)
      minFromEntry.emplace(e, 0);
    unsigned dist = 0;
    while (!frontier.empty() && dist + 1 < maxDepth) {
      ++dist;
      std::vector<SId> next;
      for (SId node : frontier) {
        for (const auto &edge : graph.calleeRefsOf(node)) {
          if (minFromEntry.emplace(edge.callee, dist).second)
            next.push_back(edge.callee);
        }
      }
      frontier = std::move(next);
    }
  }
  {
    auto tmit = minFromEntry.find(*targetId);
    if (tmit == minFromEntry.end() || tmit->second + 1 > maxDepth)
      return out; // no entry->target corridor within budget
  }

  LockDfsState st{graph,    entrySet, minFromEntry,
                  maxDepth, maxFanIn, interner.find("<indirect>")};
  st.path.push_back(*targetId);
  lockReverseDfs(st);
  truncated = st.truncated;

  // Resolve ids only for the emitted paths and skipped hubs.
  out.reserve(st.out.size());
  for (const auto &rp : st.out) {
    PathResult pr;
    pr.entryPoint = interner.resolve(rp.entryPoint);
    pr.path.reserve(rp.nodes.size());
    for (auto it = rp.nodes.rbegin(); it != rp.nodes.rend(); ++it)
      pr.path.push_back(interner.resolve(*it));
    std::unordered_set<LockKey, LockKeyHash> seen;
    for (SId site : rp.edgeSites)
      collectLocksOnEdge(cfIndex, interner.resolve(site), pr.locksHeld,
                         seen);
    out.push_back(std::move(pr));
  }
  for (const auto &[hubId, deg] : st.skippedHubs)
    skippedHubs.emplace(interner.resolve(hubId), deg);
  return out;
}

static llvm::json::Value pathResultToJson(const PathResult &pr) {
  llvm::json::Object obj;
  obj["entryPoint"] = pr.entryPoint;
  llvm::json::Array p;
  for (const auto &f : pr.path)
    p.push_back(f);
  obj["path"] = std::move(p);
  llvm::json::Array locks;
  for (const auto &l : pr.locksHeld) {
    llvm::json::Object lo;
    lo["typeName"] = l.typeName;
    lo["varName"] = l.varName;
    lo["heldAt"] = l.heldAt;
    locks.push_back(llvm::json::Value(std::move(lo)));
  }
  obj["locksHeld"] = std::move(locks);
  return llvm::json::Value(std::move(obj));
}

} // namespace

static llvm::json::Value handleQueryLocksHeld(const llvm::json::Object &args,
                                              const ToolContext &ctx) {
  std::optional<llvm::json::Value> ambiguous;
  auto ident = resolveIdentity(args, ctx, "function", "usr", ambiguous);
  if (ambiguous)
    return std::move(*ambiguous);
  if (!ident)
    return errorResult("Missing required parameter 'function' (or 'usr')");

  unsigned maxDepth = kDefaultMaxDepth;
  if (auto md = args.getInteger("max_depth"))
    maxDepth = static_cast<unsigned>(std::max<int64_t>(1, *md));

  std::vector<std::string> entryPoints;
  if (auto *epsArr = args.getArray("entry_points")) {
    for (auto &v : *epsArr) {
      if (auto s = v.getAsString())
        entryPoints.push_back(s->str());
    }
  }
  if (entryPoints.empty())
    entryPoints = ctx.entryPoints;

  size_t maxFanIn = kDefaultMaxFanIn;
  if (auto mf = args.getInteger("max_fan_in"))
    maxFanIn = static_cast<size_t>(std::max<int64_t>(0, *mf));

  bool truncated = false;
  std::map<std::string, size_t> skippedHubs;
  // The lock walk resolves the target through interner.find: a USR string
  // works verbatim (usrs ARE the interned identities).
  auto paths = collectLocksHeld(ctx.graph, ctx.cfIndex, *ident,
                                 entryPoints, maxDepth, maxFanIn,
                                 skippedHubs, truncated);

  llvm::json::Object out;
  auto fn = args.getString("function");
  out["function"] = fn ? fn->str() : *ident;
  attachUsr(out, ctx, *ident);
  llvm::json::Array arr;
  for (const auto &pr : paths)
    arr.push_back(pathResultToJson(pr));
  out["paths"] = std::move(arr);
  out["truncated"] = truncated;
  out["pathCount"] = static_cast<int64_t>(paths.size());
  if (!skippedHubs.empty()) {
    llvm::json::Array hubs;
    for (const auto &[name, inDegree] : skippedHubs) {
      llvm::json::Object hub;
      hub["name"] = name;
      hub["inDegree"] = static_cast<int64_t>(inDegree);
      hubs.push_back(llvm::json::Value(std::move(hub)));
    }
    out["skippedHubs"] = std::move(hubs);
  }
  return llvm::json::Value(std::move(out));
}

// ============================================================================
// Tool 6c: query_same_lock  —  intersection of locks_held(a) and
// locks_held(b). Lock identity = (typeName, varName).
// ============================================================================

static llvm::json::Value handleQuerySameLock(const llvm::json::Object &args,
                                             const ToolContext &ctx) {
  std::optional<llvm::json::Value> ambiguous;
  auto a = resolveIdentity(args, ctx, "fn_a", "fn_a_usr", ambiguous);
  if (ambiguous)
    return std::move(*ambiguous);
  auto b = resolveIdentity(args, ctx, "fn_b", "fn_b_usr", ambiguous);
  if (ambiguous)
    return std::move(*ambiguous);
  if (!a || !b)
    return errorResult(
        "Missing required parameters 'fn_a' and 'fn_b' (or their *_usr "
        "twins)");

  unsigned maxDepth = kDefaultMaxDepth;
  if (auto md = args.getInteger("max_depth"))
    maxDepth = static_cast<unsigned>(std::max<int64_t>(1, *md));

  std::vector<std::string> entryPoints;
  if (auto *epsArr = args.getArray("entry_points")) {
    for (auto &v : *epsArr) {
      if (auto s = v.getAsString())
        entryPoints.push_back(s->str());
    }
  }
  if (entryPoints.empty())
    entryPoints = ctx.entryPoints;

  size_t maxFanIn = kDefaultMaxFanIn;
  if (auto mf = args.getInteger("max_fan_in"))
    maxFanIn = static_cast<size_t>(std::max<int64_t>(0, *mf));

  bool truncA = false, truncB = false;
  std::map<std::string, size_t> skippedHubs;
  auto pathsA = collectLocksHeld(ctx.graph, ctx.cfIndex, *a,
                                  entryPoints, maxDepth, maxFanIn,
                                  skippedHubs, truncA);
  auto pathsB = collectLocksHeld(ctx.graph, ctx.cfIndex, *b,
                                  entryPoints, maxDepth, maxFanIn,
                                  skippedHubs, truncB);

  // Collect lock identity sets per side, remembering which paths use each.
  std::unordered_map<LockKey, std::vector<size_t>, LockKeyHash> byKeyA,
      byKeyB;
  for (size_t i = 0; i < pathsA.size(); ++i)
    for (const auto &l : pathsA[i].locksHeld)
      byKeyA[{l.typeName, l.varName}].push_back(i);
  for (size_t i = 0; i < pathsB.size(); ++i)
    for (const auto &l : pathsB[i].locksHeld)
      byKeyB[{l.typeName, l.varName}].push_back(i);

  // Intersect.
  llvm::json::Array sharedArr;
  int sharedCount = 0;
  for (const auto &[key, idxsA] : byKeyA) {
    auto it = byKeyB.find(key);
    if (it == byKeyB.end())
      continue;
    ++sharedCount;
    llvm::json::Object obj;
    obj["typeName"] = key.typeName;
    obj["varName"] = key.varName;
    llvm::json::Array pa, pb;
    for (auto idx : idxsA)
      pa.push_back(pathResultToJson(pathsA[idx]));
    for (auto idx : it->second)
      pb.push_back(pathResultToJson(pathsB[idx]));
    obj["pathsA"] = std::move(pa);
    obj["pathsB"] = std::move(pb);
    sharedArr.push_back(llvm::json::Value(std::move(obj)));
  }

  llvm::json::Object out;
  auto aName = args.getString("fn_a");
  auto bName = args.getString("fn_b");
  out["fn_a"] = aName ? aName->str() : *a;
  out["fn_b"] = bName ? bName->str() : *b;
  attachUsr(out, ctx, *a, "fn_a_usr");
  attachUsr(out, ctx, *b, "fn_b_usr");
  out["sharedLocks"] = std::move(sharedArr);
  out["shared"] = sharedCount;
  out["aOnly"] =
      static_cast<int64_t>(byKeyA.size()) - static_cast<int64_t>(sharedCount);
  out["bOnly"] =
      static_cast<int64_t>(byKeyB.size()) - static_cast<int64_t>(sharedCount);
  out["truncated"] = truncA || truncB;
  if (!skippedHubs.empty()) {
    llvm::json::Array hubs;
    for (const auto &[name, inDegree] : skippedHubs) {
      llvm::json::Object hub;
      hub["name"] = name;
      hub["inDegree"] = static_cast<int64_t>(inDegree);
      hubs.push_back(llvm::json::Value(std::move(hub)));
    }
    out["skippedHubs"] = std::move(hubs);
  }
  return llvm::json::Value(std::move(out));
}

void registerLockTools(std::vector<ToolEntry> &tools) {
  // 6b. query_locks_held
  {
    llvm::json::Object props;
    props["function"] = stringProp(
        "Qualified name of the target function. Provide 'function' or "
        "'usr' (usr wins when both are present).");
    props["usr"] = stringProp(
        "Exact USR of the target function. Bypasses name resolution — use "
        "it to pick one overload/specialization when the name is "
        "ambiguous.");
    addIdentityRefinementProps(props, "");
    props["max_depth"] = intProp(
        "Maximum number of frames above the target to walk (default: 20)");
    props["max_fan_in"] = intProp(
        "Skip expanding functions with more stored callers than this; "
        "skipped hubs are listed in the response. 0 disables "
        "(default: 1000).");
    props["entry_points"] = stringArrayProp(
        "Entry points to root the reverse walk (default: configured "
        "entry points)");
    llvm::json::Object schema;
    schema["type"] = "object";
    schema["properties"] = std::move(props);

    tools.push_back({"query_locks_held",
                     "For each entry point, enumerate call paths reaching "
                     "`function` via reverse-walking the call graph, and "
                     "report Lock-kind RAII locals live on any edge of each "
                     "path. Result: {paths:[{entryPoint, path:[fn...], "
                     "locksHeld:[{typeName, varName, heldAt}]}], truncated, "
                     "pathCount}. Truncated at 512 paths total. Walks only "
                     "through edges with stable callee identity "
                     "(indirect/function-pointer targets are skipped). An "
                     "ambiguous name returns {ambiguous:true, "
                     "candidates:[...]} — re-query with 'usr'.",
                     llvm::json::Value(std::move(schema)),
                     handleQueryLocksHeld});
  }

  // 6c. query_same_lock
  {
    llvm::json::Object props;
    props["fn_a"] = stringProp(
        "First function qualified name. Provide 'fn_a' or 'fn_a_usr' "
        "(the usr wins when both are present).");
    props["fn_a_usr"] = stringProp(
        "Exact USR of the first function. Bypasses name resolution for "
        "'fn_a' when the name is ambiguous.");
    addIdentityRefinementProps(props, "fn_a_");
    props["fn_b"] = stringProp(
        "Second function qualified name. Provide 'fn_b' or 'fn_b_usr' "
        "(the usr wins when both are present).");
    props["fn_b_usr"] = stringProp(
        "Exact USR of the second function. Bypasses name resolution for "
        "'fn_b' when the name is ambiguous.");
    addIdentityRefinementProps(props, "fn_b_");
    props["max_depth"] = intProp(
        "Maximum number of frames above each target to walk (default: 20)");
    props["max_fan_in"] = intProp(
        "Skip expanding functions with more stored callers than this; "
        "skipped hubs are listed in the response. 0 disables "
        "(default: 1000).");
    props["entry_points"] = stringArrayProp(
        "Entry points to root the reverse walk (default: configured "
        "entry points)");
    llvm::json::Object schema;
    schema["type"] = "object";
    schema["properties"] = std::move(props);

    tools.push_back({"query_same_lock",
                     "Compute the intersection of locks held across paths "
                     "reaching fn_a and fn_b. Lock identity is the tuple "
                     "(typeName, varName); the same physical mutex under "
                     "different variable names will not match. Result: "
                     "{sharedLocks:[{typeName, varName, pathsA, pathsB}], "
                     "shared, aOnly, bOnly, truncated}. An ambiguous name "
                     "returns {ambiguous:true, candidates:[...]} — re-query "
                     "with the *_usr parameter.",
                     llvm::json::Value(std::move(schema)),
                     handleQuerySameLock});
  }
}

} // namespace vycor

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
#pragma once

#include "vycor/callgraph/CallGraph.h"
#include "vycor/callgraph/ChannelIndex.h"
#include "vycor/callgraph/ControlFlowIndex.h"
#include "vycor/callgraph/ControlFlowOracle.h"

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/JSON.h"

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// Transport-neutral query tools over the baked indexes. Every tool is a
// pure function (args, context) -> JSON payload; the MCP server
// (vycor/mcp/) and the CLI are thin adapters that own the indexes and
// translate this contract onto their wire format.

namespace vycor {

/// Whole-graph query results cached across tool calls. Owned by the adapter
/// (McpServer, the CLI batch loop) and cleared wholesale whenever the
/// indexes mutate (reindex_tu), so a cached value is always consistent with
/// the graph it was computed from. Used by handlers whose cost scales with
/// the whole graph rather than the query (analyze_dead_code reruns full
/// liveness; graph_summary materializes calleesOf for every node).
struct QueryCache {
  // Final JSON results (argument-independent queries, e.g. graph_summary).
  std::map<std::string, llvm::json::Value> byKey;
  // Typed intermediate results shared across argument variations (e.g. the
  // dead-code liveness map, reused by every pagination/filter combination).
  std::map<std::string, std::shared_ptr<void>> objects;

  void clear() {
    byKey.clear();
    objects.clear();
  }
};

/// Context passed to every tool handler.
struct ToolContext {
  const CallGraph &graph;
  const ControlFlowOracle &oracle;
  const ControlFlowIndex &cfIndex;
  const std::vector<std::string> &entryPoints;
  /// Channel/data-flow index (list_channels, query_channel,
  /// query_channels_for_function, explain_ordering). Null when the adapter
  /// was started without a --channel-types-json config — trailing default
  /// so every existing positional ToolContext{...} call site (this repo
  /// has 30+, mostly in tests) keeps compiling unchanged; channel tool
  /// handlers must null-check it themselves.
  const ChannelIndex *channels = nullptr;
  /// Optional whole-graph result cache; null in contexts that do not want
  /// caching (handlers must treat it as best-effort).
  QueryCache *cache = nullptr;
};

/// Signature for a tool handler function.
///
/// Result contract (shared by every transport):
///   - success: the payload object itself, no envelope;
///   - error:   `{"error": "<message>"}` (see errorResult / isErrorResult);
///   - ambiguous identity: a non-error payload with `"ambiguous": true` and
///     a `candidates` list (see isAmbiguousResult and Identity.h).
using ToolHandler =
    std::function<llvm::json::Value(const llvm::json::Object &args,
                                    const ToolContext &ctx)>;

/// Descriptor for a single query tool.
struct ToolEntry {
  std::string name;
  std::string description;
  llvm::json::Value inputSchema; // JSON Schema object
  ToolHandler handler;           // null for adapter-implemented tools (reindex_tu)
};

/// Returns the list of all registered tools, in tools/list order.
std::vector<ToolEntry> getRegisteredTools();

/// Build an error payload: `{"error": message}`.
llvm::json::Value errorResult(llvm::StringRef message);
/// The error message when `result` is an error payload, else nullopt.
std::optional<llvm::StringRef> errorMessage(const llvm::json::Value &result);
bool isErrorResult(const llvm::json::Value &result);
/// True for the non-error disambiguation payload (`"ambiguous": true`).
bool isAmbiguousResult(const llvm::json::Value &result);

} // namespace vycor

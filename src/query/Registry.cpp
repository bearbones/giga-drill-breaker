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
#include "Registry.h"

#include <map>

namespace vycor {

llvm::json::Value errorResult(llvm::StringRef message) {
  llvm::json::Object obj;
  obj["error"] = message.str();
  return llvm::json::Value(std::move(obj));
}

std::optional<llvm::StringRef> errorMessage(const llvm::json::Value &result) {
  const auto *obj = result.getAsObject();
  if (!obj)
    return std::nullopt;
  return obj->getString("error");
}

bool isErrorResult(const llvm::json::Value &result) {
  return errorMessage(result).has_value();
}

bool isAmbiguousResult(const llvm::json::Value &result) {
  const auto *obj = result.getAsObject();
  if (!obj)
    return false;
  auto b = obj->getBoolean("ambiguous");
  return b && *b;
}

// The list-shaped member of each tool's payload (ToolEntry::recordsKey).
// Tools absent here answer with one scalar record — including query_channel,
// whose producers/consumers lists are peers (naming one would report a
// consumer-only channel as empty). Where a payload carries a primary list
// plus a secondary one (analyze_dead_code, get_class_hierarchy) the primary
// is named; the other stays in the ndjson _summary line.
// Sections beyond the graph a tool reads (ToolEntry::needs). Exception
// and lock tools walk call-site contexts; channel tools read the channel
// index; everything else, graph_summary included (it reports the header
// counts), touches only the graph.
static const std::map<std::string, unsigned> kExtraNeeds = {
    {"query_exception_safety", kSectionControlFlow},
    {"query_call_site_context", kSectionControlFlow},
    {"query_raii_scopes_at_callsite", kSectionControlFlow},
    {"query_throw_propagation", kSectionControlFlow},
    {"query_all_path_contexts", kSectionControlFlow},
    {"query_nearest_catches", kSectionControlFlow},
    {"query_locks_held", kSectionControlFlow},
    {"query_same_lock", kSectionControlFlow},
    {"list_channels", kSectionChannels},
    {"query_channel", kSectionChannels},
    {"query_channels_for_function", kSectionChannels},
    {"explain_ordering", kSectionChannels},
};

static const std::map<std::string, std::string> kRecordsKeys = {
    {"search_functions", "matches"},
    {"get_callees", "callees"},
    {"get_callers", "callers"},
    {"find_call_chain", "paths"},
    {"get_class_hierarchy", "derivedClasses"},
    {"list_entry_points", "entryPoints"},
    {"list_callback_sites", "targets"},
    {"list_concurrency_entry_points", "entries"},
    {"query_raii_scopes_at_callsite", "locals"},
    {"query_throw_propagation", "paths"},
    {"query_all_path_contexts", "paths"},
    {"query_nearest_catches", "catches"},
    {"query_locks_held", "paths"},
    {"query_same_lock", "sharedLocks"},
    {"analyze_dead_code", "dead"},
    {"list_channels", "channels"},
    {"query_channels_for_function", "sites"},
};

std::vector<std::string> sectionNames(unsigned needs) {
  std::vector<std::string> out;
  if (needs & kSectionGraph)
    out.push_back("graph");
  if (needs & kSectionControlFlow)
    out.push_back("control_flow");
  if (needs & kSectionChannels)
    out.push_back("channels");
  return out;
}

std::vector<ToolEntry> getRegisteredTools() {
  std::vector<ToolEntry> tools;
  registerGraphTools(tools);
  registerExceptionTools(tools);
  registerLockTools(tools);
  registerDeadCodeTools(tools);
  registerChannelTools(tools);
  for (auto &tool : tools) {
    auto it = kRecordsKeys.find(tool.name);
    if (it != kRecordsKeys.end())
      tool.recordsKey = it->second;
    auto needs = kExtraNeeds.find(tool.name);
    tool.needs = kSectionGraph |
                 (needs != kExtraNeeds.end() ? needs->second : 0u);
  }

  // reindex_tu — handler is null: it mutates the indexes, so each adapter
  // (McpServer today) implements it against its own owned state.
  {
    llvm::json::Object schema;
    schema["type"] = "object";
    schema["required"] = llvm::json::Array{"file"};
    llvm::json::Object props;
    props["file"] = llvm::json::Object{
        {"type", "string"},
        {"description", "Absolute path of the TU to re-index"}};
    schema["properties"] = std::move(props);

    tools.push_back({"reindex_tu",
                     "Re-index a single translation unit after source changes. "
                     "Removes stale edges/contexts and re-runs all three "
                     "analysis phases for the given file. Returns counts of "
                     "edges and contexts removed and current totals.",
                     llvm::json::Value(std::move(schema)),
                     nullptr});
    tools.back().needs = kSectionAll; // mutates every index
  }

  return tools;
}

} // namespace vycor

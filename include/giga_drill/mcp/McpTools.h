#pragma once

#include "giga_drill/callgraph/CallGraph.h"
#include "giga_drill/callgraph/ControlFlowIndex.h"
#include "giga_drill/callgraph/ControlFlowOracle.h"

#include "llvm/Support/JSON.h"

#include <functional>
#include <string>
#include <vector>

namespace giga_drill {

/// Context passed to every tool handler.
struct McpToolContext {
  const CallGraph &graph;
  const ControlFlowOracle &oracle;
  const ControlFlowIndex &cfIndex;
  const std::vector<std::string> &entryPoints;
};

/// Signature for a tool handler function.
using McpToolHandler =
    std::function<llvm::json::Value(const llvm::json::Object &args,
                                    const McpToolContext &ctx)>;

/// Descriptor for a single MCP tool.
struct McpToolEntry {
  std::string name;
  std::string description;
  llvm::json::Value inputSchema; // JSON Schema object
  McpToolHandler handler;
};

/// Returns the list of all registered tools.
std::vector<McpToolEntry> getRegisteredTools();

} // namespace giga_drill

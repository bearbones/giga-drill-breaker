#pragma once

#include "giga_drill/callgraph/CallGraph.h"
#include "giga_drill/callgraph/ControlFlowIndex.h"
#include "giga_drill/callgraph/ControlFlowOracle.h"
#include "giga_drill/mcp/McpProtocol.h"

#include <string>
#include <vector>

namespace giga_drill {

class McpServer {
public:
  McpServer(CallGraph graph, ControlFlowIndex cfIndex,
            std::vector<std::string> entryPoints);

  /// Run the MCP stdio loop. Returns 0 on clean shutdown.
  int run();

private:
  CallGraph graph_;
  ControlFlowIndex cfIndex_;
  ControlFlowOracle oracle_;
  std::vector<std::string> entryPoints_;
  bool initialized_ = false;

  void dispatch(const McpRequest &req);

  llvm::json::Value handleInitialize(const llvm::json::Object &params);
  llvm::json::Value handleToolsList();
  llvm::json::Value handleToolsCall(const llvm::json::Object &params);
};

} // namespace giga_drill

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

#include "giga_drill/callgraph/CallGraph.h"
#include "giga_drill/callgraph/ControlFlowIndex.h"
#include "giga_drill/callgraph/ControlFlowOracle.h"
#include "giga_drill/mcp/McpProtocol.h"

#include <string>
#include <vector>

namespace giga_drill {

class McpServer {
public:
  McpServer(CallGraph &&graph, ControlFlowIndex &&cfIndex,
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

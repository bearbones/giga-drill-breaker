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

std::vector<ToolEntry> getRegisteredTools() {
  std::vector<ToolEntry> tools;
  registerGraphTools(tools);
  registerExceptionTools(tools);
  registerLockTools(tools);
  registerDeadCodeTools(tools);
  registerChannelTools(tools);

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
  }

  return tools;
}

} // namespace vycor

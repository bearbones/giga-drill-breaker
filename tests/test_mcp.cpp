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

// test_mcp.cpp — Tests for the MCP call graph server.

#include "giga_drill/callgraph/CallGraph.h"
#include "giga_drill/callgraph/ControlFlowIndex.h"
#include "giga_drill/callgraph/ControlFlowOracle.h"
#include "giga_drill/mcp/McpProtocol.h"
#include "giga_drill/mcp/McpTools.h"

#include "llvm/Support/JSON.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <cstring>
#include <string>

using namespace giga_drill;

// ============================================================================
// Helper: build a simple test graph
//
//   main -> processFile -> readData
//                       -> writeLog
//   main -> cleanup
//
//   Base (virtual: doWork) -> Derived (override: doWork)
//   main -> Base::doWork (VirtualDispatch)
// ============================================================================

static CallGraph buildTestGraph() {
  CallGraph g;

  g.addNode({"main", "main.cpp", 10, true, false, ""});
  g.addNode({"processFile", "process.cpp", 20, false, false, ""});
  g.addNode({"readData", "io.cpp", 30, false, false, ""});
  g.addNode({"writeLog", "log.cpp", 40, false, false, ""});
  g.addNode({"cleanup", "main.cpp", 50, false, false, ""});
  g.addNode({"Base::doWork", "base.cpp", 60, false, true, "Base"});
  g.addNode({"Derived::doWork", "derived.cpp", 70, false, true, "Derived"});
  g.addNode({"orphanFunc", "orphan.cpp", 80, false, false, ""});

  g.addEdge({"main", "processFile", EdgeKind::DirectCall,
             Confidence::Proven, "main.cpp:11:3", 0});
  g.addEdge({"main", "cleanup", EdgeKind::DirectCall,
             Confidence::Proven, "main.cpp:12:3", 0});
  g.addEdge({"processFile", "readData", EdgeKind::DirectCall,
             Confidence::Proven, "process.cpp:21:5", 0});
  g.addEdge({"processFile", "writeLog", EdgeKind::DirectCall,
             Confidence::Plausible, "process.cpp:22:5", 0});
  g.addEdge({"main", "Base::doWork", EdgeKind::VirtualDispatch,
             Confidence::Plausible, "main.cpp:13:3", 0});

  g.addDerivedClass("Base", "Derived");
  g.addMethodOverride("Base::doWork", "Derived::doWork");

  return g;
}

// Helper: build a test control flow index with a protected and unprotected call.
static ControlFlowIndex buildTestCfIndex() {
  ControlFlowIndex idx;

  // processFile -> readData is inside a try/catch.
  {
    CallSiteContext ctx;
    ctx.callerName = "processFile";
    ctx.calleeName = "readData";
    ctx.callSite = "process.cpp:21:5";
    TryCatchScope scope;
    scope.tryLocation = "process.cpp:20:3";
    scope.enclosingFunction = "processFile";
    scope.nestingDepth = 0;
    CatchHandlerInfo handler;
    handler.caughtType = "std::exception";
    handler.location = "process.cpp:25:3";
    scope.handlers.push_back(std::move(handler));
    ctx.enclosingTryCatches.push_back(std::move(scope));
    idx.addCallSiteContext(std::move(ctx));
  }

  // main -> processFile is NOT inside a try/catch.
  {
    CallSiteContext ctx;
    ctx.callerName = "main";
    ctx.calleeName = "processFile";
    ctx.callSite = "main.cpp:11:3";
    idx.addCallSiteContext(std::move(ctx));
  }

  return idx;
}

// Helper: extract the text from an MCP tool result.
static llvm::json::Object parseToolResult(const llvm::json::Value &result) {
  auto *obj = result.getAsObject();
  REQUIRE(obj != nullptr);
  auto *content = obj->getArray("content");
  REQUIRE(content != nullptr);
  REQUIRE(content->size() >= 1);
  auto *first = (*content)[0].getAsObject();
  REQUIRE(first != nullptr);
  auto text = first->getString("text");
  REQUIRE(text.has_value());

  auto parsed = llvm::json::parse(*text);
  REQUIRE(static_cast<bool>(parsed));
  auto *parsedObj = parsed->getAsObject();
  REQUIRE(parsedObj != nullptr);
  return std::move(*parsedObj);
}

static bool isErrorResult(const llvm::json::Value &result) {
  auto *obj = result.getAsObject();
  if (!obj)
    return false;
  if (auto b = obj->getBoolean("isError"))
    return *b;
  return false;
}

// ============================================================================
// Protocol tests
// ============================================================================

TEST_CASE("McpRequest isNotification", "[mcp][protocol]") {
  McpRequest req;

  SECTION("null id is a notification") {
    req.id = nullptr;
    CHECK(req.isNotification());
  }

  SECTION("integer id is not a notification") {
    req.id = 1;
    CHECK_FALSE(req.isNotification());
  }

  SECTION("string id is not a notification") {
    req.id = "abc";
    CHECK_FALSE(req.isNotification());
  }
}

TEST_CASE("readRequest parses Content-Length framed messages",
          "[mcp][protocol]") {
  SECTION("valid request") {
    std::string input =
        "Content-Length: 59\r\n"
        "\r\n"
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{}}";

    FILE *f = fmemopen(const_cast<char *>(input.data()), input.size(), "r");
    REQUIRE(f != nullptr);

    auto req = readRequest(f, llvm::errs());
    std::fclose(f);

    REQUIRE(req.has_value());
    CHECK(req->method == "initialize");
    auto id = req->id.getAsInteger();
    REQUIRE(id.has_value());
    CHECK(*id == 1);
  }

  SECTION("EOF returns nullopt") {
    std::string input;
    FILE *f = fmemopen(const_cast<char *>(input.data()), 0, "r");
    // fmemopen with size 0 may return NULL on some platforms.
    if (!f) {
      // Just verify we handle it.
      CHECK(true);
      return;
    }
    auto req = readRequest(f, llvm::errs());
    std::fclose(f);
    CHECK_FALSE(req.has_value());
  }

  SECTION("notification has null id") {
    std::string input =
        "Content-Length: 50\r\n"
        "\r\n"
        "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}";

    FILE *f = fmemopen(const_cast<char *>(input.data()), input.size(), "r");
    REQUIRE(f != nullptr);

    auto req = readRequest(f, llvm::errs());
    std::fclose(f);

    REQUIRE(req.has_value());
    CHECK(req->method == "notifications/initialized");
    CHECK(req->isNotification());
  }
}

// ============================================================================
// Tool registration tests
// ============================================================================

TEST_CASE("getRegisteredTools returns all 8 tools", "[mcp][tools]") {
  auto tools = getRegisteredTools();
  CHECK(tools.size() == 8);

  // Verify tool names.
  std::set<std::string> names;
  for (auto &t : tools)
    names.insert(t.name);

  CHECK(names.count("lookup_function") == 1);
  CHECK(names.count("get_callees") == 1);
  CHECK(names.count("get_callers") == 1);
  CHECK(names.count("find_call_chain") == 1);
  CHECK(names.count("query_exception_safety") == 1);
  CHECK(names.count("query_call_site_context") == 1);
  CHECK(names.count("analyze_dead_code") == 1);
  CHECK(names.count("get_class_hierarchy") == 1);
}

// ============================================================================
// Tool handler tests
// ============================================================================

TEST_CASE("lookup_function tool", "[mcp][tools]") {
  auto graph = buildTestGraph();
  ControlFlowIndex cfIndex;
  ControlFlowOracle oracle(graph, cfIndex);
  std::vector<std::string> eps = {"main"};
  McpToolContext ctx{graph, oracle, cfIndex, eps};

  auto tools = getRegisteredTools();
  McpToolHandler handler;
  for (auto &t : tools) {
    if (t.name == "lookup_function") {
      handler = t.handler;
      break;
    }
  }
  REQUIRE(handler);

  SECTION("existing function") {
    llvm::json::Object args;
    args["name"] = "processFile";
    auto result = handler(args, ctx);
    CHECK_FALSE(isErrorResult(result));

    auto obj = parseToolResult(result);
    CHECK(obj.getString("qualifiedName") == "processFile");
    CHECK(obj.getString("file") == "process.cpp");
    CHECK(obj.getInteger("line") == 20);
  }

  SECTION("missing function") {
    llvm::json::Object args;
    args["name"] = "nonexistent";
    auto result = handler(args, ctx);
    CHECK(isErrorResult(result));
  }

  SECTION("missing parameter") {
    llvm::json::Object args;
    auto result = handler(args, ctx);
    CHECK(isErrorResult(result));
  }
}

TEST_CASE("get_callees tool", "[mcp][tools]") {
  auto graph = buildTestGraph();
  ControlFlowIndex cfIndex;
  ControlFlowOracle oracle(graph, cfIndex);
  std::vector<std::string> eps = {"main"};
  McpToolContext ctx{graph, oracle, cfIndex, eps};

  auto tools = getRegisteredTools();
  McpToolHandler handler;
  for (auto &t : tools) {
    if (t.name == "get_callees") {
      handler = t.handler;
      break;
    }
  }
  REQUIRE(handler);

  SECTION("main has 3 callees") {
    llvm::json::Object args;
    args["name"] = "main";
    auto result = handler(args, ctx);
    auto obj = parseToolResult(result);
    CHECK(obj.getInteger("calleeCount") == 3);
  }

  SECTION("filter by edge kind") {
    llvm::json::Object args;
    args["name"] = "main";
    llvm::json::Array kinds;
    kinds.push_back("DirectCall");
    args["edge_kinds"] = std::move(kinds);
    auto result = handler(args, ctx);
    auto obj = parseToolResult(result);
    CHECK(obj.getInteger("calleeCount") == 2); // processFile, cleanup
  }

  SECTION("filter by min confidence") {
    llvm::json::Object args;
    args["name"] = "processFile";
    args["min_confidence"] = "Proven";
    auto result = handler(args, ctx);
    auto obj = parseToolResult(result);
    CHECK(obj.getInteger("calleeCount") == 1); // only readData is Proven
  }
}

TEST_CASE("get_callers tool", "[mcp][tools]") {
  auto graph = buildTestGraph();
  ControlFlowIndex cfIndex;
  ControlFlowOracle oracle(graph, cfIndex);
  std::vector<std::string> eps = {"main"};
  McpToolContext ctx{graph, oracle, cfIndex, eps};

  auto tools = getRegisteredTools();
  McpToolHandler handler;
  for (auto &t : tools) {
    if (t.name == "get_callers") {
      handler = t.handler;
      break;
    }
  }
  REQUIRE(handler);

  SECTION("processFile has 1 caller") {
    llvm::json::Object args;
    args["name"] = "processFile";
    auto result = handler(args, ctx);
    auto obj = parseToolResult(result);
    CHECK(obj.getInteger("callerCount") == 1);
  }

  SECTION("orphanFunc has 0 callers") {
    llvm::json::Object args;
    args["name"] = "orphanFunc";
    auto result = handler(args, ctx);
    auto obj = parseToolResult(result);
    CHECK(obj.getInteger("callerCount") == 0);
  }
}

TEST_CASE("find_call_chain tool", "[mcp][tools]") {
  auto graph = buildTestGraph();
  ControlFlowIndex cfIndex;
  ControlFlowOracle oracle(graph, cfIndex);
  std::vector<std::string> eps = {"main"};
  McpToolContext ctx{graph, oracle, cfIndex, eps};

  auto tools = getRegisteredTools();
  McpToolHandler handler;
  for (auto &t : tools) {
    if (t.name == "find_call_chain") {
      handler = t.handler;
      break;
    }
  }
  REQUIRE(handler);

  SECTION("chain from main to readData") {
    llvm::json::Object args;
    args["to"] = "readData";
    auto result = handler(args, ctx);
    auto obj = parseToolResult(result);
    auto pathCount = obj.getInteger("pathCount");
    REQUIRE(pathCount.has_value());
    CHECK(*pathCount >= 1);

    auto *paths = obj.getArray("paths");
    REQUIRE(paths != nullptr);
    // First path should be [main, processFile, readData].
    auto *firstPath = (*paths)[0].getAsArray();
    REQUIRE(firstPath != nullptr);
    CHECK(firstPath->size() == 3);
  }

  SECTION("no chain to orphan") {
    llvm::json::Object args;
    args["to"] = "orphanFunc";
    auto result = handler(args, ctx);
    auto obj = parseToolResult(result);
    CHECK(obj.getInteger("pathCount") == 0);
  }

  SECTION("explicit from parameter") {
    llvm::json::Object args;
    args["from"] = "processFile";
    args["to"] = "readData";
    auto result = handler(args, ctx);
    auto obj = parseToolResult(result);
    CHECK(obj.getInteger("pathCount") == 1);
  }
}

TEST_CASE("query_call_site_context tool", "[mcp][tools]") {
  auto graph = buildTestGraph();
  auto cfIndex = buildTestCfIndex();
  ControlFlowOracle oracle(graph, cfIndex);
  std::vector<std::string> eps = {"main"};
  McpToolContext ctx{graph, oracle, cfIndex, eps};

  auto tools = getRegisteredTools();
  McpToolHandler handler;
  for (auto &t : tools) {
    if (t.name == "query_call_site_context") {
      handler = t.handler;
      break;
    }
  }
  REQUIRE(handler);

  SECTION("protected call site") {
    llvm::json::Object args;
    args["call_site"] = "process.cpp:21:5";
    auto result = handler(args, ctx);
    auto obj = parseToolResult(result);
    CHECK(obj.getBoolean("isUnderTryCatch") == true);
    CHECK(obj.getString("caller") == "processFile");
    CHECK(obj.getString("callee") == "readData");
  }

  SECTION("unprotected call site") {
    llvm::json::Object args;
    args["call_site"] = "main.cpp:11:3";
    auto result = handler(args, ctx);
    auto obj = parseToolResult(result);
    CHECK(obj.getBoolean("isUnderTryCatch") == false);
  }
}

TEST_CASE("analyze_dead_code tool", "[mcp][tools]") {
  auto graph = buildTestGraph();
  ControlFlowIndex cfIndex;
  ControlFlowOracle oracle(graph, cfIndex);
  std::vector<std::string> eps = {"main"};
  McpToolContext ctx{graph, oracle, cfIndex, eps};

  auto tools = getRegisteredTools();
  McpToolHandler handler;
  for (auto &t : tools) {
    if (t.name == "analyze_dead_code") {
      handler = t.handler;
      break;
    }
  }
  REQUIRE(handler);

  SECTION("orphanFunc is dead") {
    llvm::json::Object args;
    auto result = handler(args, ctx);
    auto obj = parseToolResult(result);

    auto deadCount = obj.getInteger("deadCount");
    REQUIRE(deadCount.has_value());
    CHECK(*deadCount >= 1);

    // Verify orphanFunc is in the dead list.
    auto *deadArr = obj.getArray("dead");
    REQUIRE(deadArr != nullptr);
    bool foundOrphan = false;
    for (auto &entry : *deadArr) {
      if (auto *entryObj = entry.getAsObject()) {
        if (auto name = entryObj->getString("name")) {
          if (*name == "orphanFunc")
            foundOrphan = true;
        }
      }
    }
    CHECK(foundOrphan);
  }
}

TEST_CASE("get_class_hierarchy tool", "[mcp][tools]") {
  auto graph = buildTestGraph();
  ControlFlowIndex cfIndex;
  ControlFlowOracle oracle(graph, cfIndex);
  std::vector<std::string> eps = {"main"};
  McpToolContext ctx{graph, oracle, cfIndex, eps};

  auto tools = getRegisteredTools();
  McpToolHandler handler;
  for (auto &t : tools) {
    if (t.name == "get_class_hierarchy") {
      handler = t.handler;
      break;
    }
  }
  REQUIRE(handler);

  SECTION("Base has Derived") {
    llvm::json::Object args;
    args["class_name"] = "Base";
    auto result = handler(args, ctx);
    auto obj = parseToolResult(result);
    CHECK(obj.getInteger("derivedClassCount") == 1);
  }

  SECTION("with overrides") {
    llvm::json::Object args;
    args["class_name"] = "Base";
    args["include_overrides"] = true;
    auto result = handler(args, ctx);
    auto obj = parseToolResult(result);

    auto *overrides = obj.getArray("virtualMethodOverrides");
    REQUIRE(overrides != nullptr);
    CHECK(overrides->size() >= 1);
  }

  SECTION("nonexistent class returns empty") {
    llvm::json::Object args;
    args["class_name"] = "NoSuchClass";
    auto result = handler(args, ctx);
    auto obj = parseToolResult(result);
    CHECK(obj.getInteger("derivedClassCount") == 0);
  }
}

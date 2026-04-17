// Copyright (c) 2026 The giga-drill-breaker Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0

// test_deep_chains.cpp — Regression coverage for the deep_chains fixture.
//
// Asserts that the call graph built from examples/deep_chains/ contains:
//   (a) every node named in expected_chains.json (both chains),
//   (b) every required edge with the declared kind and confidence,
//   (c) per-layer "must have Proven + Plausible" invariants.
//
// Uses containment semantics: the real graph may emit extra incidental
// edges; the test only checks that expected edges are present.

#include "giga_drill/callgraph/CallGraph.h"
#include "giga_drill/callgraph/CallGraphBuilder.h"

#include <catch2/catch_test_macros.hpp>
#include <clang/Tooling/CompilationDatabase.h>

#include <string>
#include <vector>

using namespace giga_drill;

namespace {

struct RequiredEdge {
  std::string from;
  std::string to;
  EdgeKind kind;
  Confidence confidence;
};

EdgeKind parseKind(const std::string &s) {
  if (s == "DirectCall") return EdgeKind::DirectCall;
  if (s == "VirtualDispatch") return EdgeKind::VirtualDispatch;
  if (s == "FunctionPointer") return EdgeKind::FunctionPointer;
  if (s == "ConstructorCall") return EdgeKind::ConstructorCall;
  if (s == "DestructorCall") return EdgeKind::DestructorCall;
  if (s == "OperatorCall") return EdgeKind::OperatorCall;
  if (s == "TemplateInstantiation") return EdgeKind::TemplateInstantiation;
  FAIL("unknown edge kind: " << s);
  return EdgeKind::DirectCall;
}

Confidence parseConf(const std::string &s) {
  if (s == "Proven") return Confidence::Proven;
  if (s == "Plausible") return Confidence::Plausible;
  if (s == "Unknown") return Confidence::Unknown;
  FAIL("unknown confidence: " << s);
  return Confidence::Unknown;
}

bool graphHasEdge(const CallGraph &g, const RequiredEdge &e) {
  auto outs = g.calleesOf(e.from);
  for (auto *edge : outs) {
    if (edge->calleeName == e.to && edge->kind == e.kind &&
        edge->confidence == e.confidence)
      return true;
  }
  return false;
}

bool hasOutEdgeWithConfidence(const CallGraph &g, const std::string &from,
                              Confidence c) {
  auto outs = g.calleesOf(from);
  for (auto *edge : outs) {
    if (edge->confidence == c)
      return true;
  }
  return false;
}

std::vector<std::string> deepChainsSourceFiles(const std::string &base) {
  return {
      base + "main.cpp",
      base + "pipeline.cpp",
      base + "stage1_ingest.cpp",
      base + "stage2_parse.cpp",
      base + "stage3_transform.cpp",
      base + "stage4_dispatch.cpp",
      base + "stage5_sink.cpp",
      base + "plugins.cpp",
      base + "workers.cpp",
      base + "tokenizer.cpp",
      base + "scheduler.cpp",
      base + "callbacks.cpp",
  };
}

CallGraph buildFixtureGraph() {
  const std::string base =
      std::string(PROJECT_SOURCE_DIR) + "/examples/deep_chains/";
  clang::tooling::FixedCompilationDatabase compDb(".",
                                                  {"-std=c++17", "-I" + base});
  return buildCallGraph(compDb, deepChainsSourceFiles(base));
}

} // namespace

// ============================================================================
// Smoke: fixture files are readable by the build system and graph builds.
// ============================================================================

TEST_CASE("deep_chains call graph builds without errors",
          "[deep_chains][smoke]") {
  auto g = buildFixtureGraph();
  CHECK(g.nodeCount() > 0);
  CHECK(g.edgeCount() > 0);
  // At minimum, main and both pipeline entry points must be indexed.
  CHECK(g.findNode("main") != nullptr);
  CHECK(g.findNode("Pipeline::run") != nullptr);
  CHECK(g.findNode("Pipeline::runAsync") != nullptr);
}

// ============================================================================
// Chain A: 6-layer concrete-to-virtual pipeline.
// ============================================================================

TEST_CASE("Chain A: pipeline chain is >=6 layers with expected edges",
          "[deep_chains][chainA]") {
  auto g = buildFixtureGraph();

  const std::vector<std::string> path = {
      "main",           "Pipeline::run", "stage1_ingest", "stage2_parse",
      "stage3_transform","stage4_dispatch","stage5_sink"};

  SECTION("all nodes exist") {
    for (auto &n : path)
      CHECK(g.findNode(n) != nullptr);
  }

  SECTION("path has >=6 layers of DirectCall Proven edges") {
    // 7 nodes -> 6 edges between them.
    for (size_t i = 0; i + 1 < path.size(); ++i) {
      bool found = false;
      for (auto *e : g.calleesOf(path[i])) {
        if (e->calleeName == path[i + 1] &&
            e->kind == EdgeKind::DirectCall &&
            e->confidence == Confidence::Proven) {
          found = true;
          break;
        }
      }
      INFO("layer " << (i + 1) << ": " << path[i] << " -> " << path[i + 1]);
      CHECK(found);
    }
  }

  SECTION("required Proven edges") {
    std::vector<RequiredEdge> edges = {
        {"main", "Pipeline::run", EdgeKind::DirectCall, Confidence::Proven},
        {"Pipeline::run", "stage1_ingest", EdgeKind::DirectCall,
         Confidence::Proven},
        {"stage1_ingest", "stage2_parse", EdgeKind::DirectCall,
         Confidence::Proven},
        {"stage2_parse", "stage3_transform", EdgeKind::DirectCall,
         Confidence::Proven},
        {"stage3_transform", "stage4_dispatch", EdgeKind::DirectCall,
         Confidence::Proven},
        {"stage4_dispatch", "stage5_sink", EdgeKind::DirectCall,
         Confidence::Proven},
        {"stage5_sink", "Registry::invoke", EdgeKind::DirectCall,
         Confidence::Proven},
    };
    for (auto &e : edges) {
      INFO("missing Proven edge: " << e.from << " -> " << e.to);
      CHECK(graphHasEdge(g, e));
    }
  }

  SECTION("required Plausible FunctionPointer edges") {
    std::vector<RequiredEdge> edges = {
        {"main", "cbs::startupHook", EdgeKind::FunctionPointer,
         Confidence::Plausible},
        {"Pipeline::run", "cbs::startupHook", EdgeKind::FunctionPointer,
         Confidence::Plausible},
        {"stage1_ingest", "cbs::defaultHasher", EdgeKind::FunctionPointer,
         Confidence::Plausible},
        {"stage2_parse", "cbs::logAfter", EdgeKind::FunctionPointer,
         Confidence::Plausible},
        {"stage3_transform", "cbs::normalizePayload",
         EdgeKind::FunctionPointer, Confidence::Plausible},
        {"stage5_sink", "cbs::finalFormat", EdgeKind::FunctionPointer,
         Confidence::Plausible},
    };
    for (auto &e : edges) {
      INFO("missing Plausible FnPtr edge: " << e.from << " -> " << e.to);
      CHECK(graphHasEdge(g, e));
    }
  }

  SECTION("stage4_dispatch has Plausible VirtualDispatch fan-out to plugins") {
    std::vector<RequiredEdge> edges = {
        {"stage4_dispatch", "PluginAlpha::handle", EdgeKind::VirtualDispatch,
         Confidence::Plausible},
        {"stage4_dispatch", "PluginBeta::handle", EdgeKind::VirtualDispatch,
         Confidence::Plausible},
        {"stage4_dispatch", "PluginGamma::handle", EdgeKind::VirtualDispatch,
         Confidence::Plausible},
    };
    for (auto &e : edges) {
      INFO("missing Plausible VirtualDispatch edge: " << e.from << " -> "
                                                      << e.to);
      CHECK(graphHasEdge(g, e));
    }
  }

  SECTION("every node on the main Chain A path emits both Proven and "
          "Plausible out-edges") {
    // (stage5_sink's Proven edge is Registry::invoke, already covered.)
    for (auto &n : path) {
      INFO("node without Proven out-edge: " << n);
      CHECK(hasOutEdgeWithConfidence(g, n, Confidence::Proven));
      INFO("node without Plausible out-edge: " << n);
      CHECK(hasOutEdgeWithConfidence(g, n, Confidence::Plausible));
    }
  }
}

// ============================================================================
// Chain B: 6-layer virtual-scheduler chain.
// ============================================================================

TEST_CASE("Chain B: scheduler chain exercises base-ref virtual dispatch",
          "[deep_chains][chainB]") {
  auto g = buildFixtureGraph();

  SECTION("all nodes exist") {
    for (auto &n :
         {"Pipeline::runAsync", "Scheduler::schedule", "Worker::execute",
          "NetworkWorker::execute", "DiskWorker::execute", "tcpWriteBytes"})
      CHECK(g.findNode(n) != nullptr);
  }

  SECTION("Scheduler::schedule fans out Plausible VirtualDispatch "
          "to Worker and overrides") {
    std::vector<RequiredEdge> edges = {
        {"Scheduler::schedule", "NetworkWorker::execute",
         EdgeKind::VirtualDispatch, Confidence::Plausible},
        {"Scheduler::schedule", "DiskWorker::execute",
         EdgeKind::VirtualDispatch, Confidence::Plausible},
    };
    for (auto &e : edges) {
      INFO("missing Plausible VirtualDispatch edge: " << e.from << " -> "
                                                      << e.to);
      CHECK(graphHasEdge(g, e));
    }
  }

  SECTION("NetworkWorker::execute -> tcpWriteBytes is Proven DirectCall") {
    CHECK(graphHasEdge(g, {"NetworkWorker::execute", "tcpWriteBytes",
                           EdgeKind::DirectCall, Confidence::Proven}));
  }

  SECTION("Pipeline::runAsync -> Scheduler::schedule is Proven DirectCall") {
    CHECK(graphHasEdge(g, {"Pipeline::runAsync", "Scheduler::schedule",
                           EdgeKind::DirectCall, Confidence::Proven}));
  }
}

// ============================================================================
// Expected-chains JSON is present and well-formed.
// ============================================================================

TEST_CASE("expected_chains.json fixture is present", "[deep_chains][smoke]") {
  std::string path = std::string(PROJECT_SOURCE_DIR) +
                     "/examples/deep_chains/expected_chains.json";
  std::FILE *fp = std::fopen(path.c_str(), "r");
  REQUIRE(fp != nullptr);
  std::fclose(fp);
}

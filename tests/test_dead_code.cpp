// test_dead_code.cpp — TDD skeleton for the dead code analyzer (Agodego).
//
// These tests drive development of the call graph library and dead code
// analyzer. Initially they are smoke tests and stubs; each section will be
// filled in as the implementation progresses.

#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <sstream>
#include <string>

static std::string readFile(const std::string &path) {
  std::ifstream in(path);
  if (!in.is_open())
    return {};
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

// ============================================================================
// Smoke tests: verify the example project files are loadable
// ============================================================================

TEST_CASE("Dead code example files are loadable", "[dead_code][smoke]") {
  const std::string base =
      std::string(PROJECT_SOURCE_DIR) + "/examples/dead_code/";

  SECTION("main.cpp exists") {
    auto content = readFile(base + "main.cpp");
    CHECK_FALSE(content.empty());
  }

  SECTION("all headers exist") {
    CHECK_FALSE(readFile(base + "api.hpp").empty());
    CHECK_FALSE(readFile(base + "shapes.hpp").empty());
    CHECK_FALSE(readFile(base + "callbacks.hpp").empty());
    CHECK_FALSE(readFile(base + "internal.hpp").empty());
  }

  SECTION("all implementation files exist") {
    CHECK_FALSE(readFile(base + "api.cpp").empty());
    CHECK_FALSE(readFile(base + "shapes.cpp").empty());
    CHECK_FALSE(readFile(base + "callbacks.cpp").empty());
    CHECK_FALSE(readFile(base + "internal.cpp").empty());
  }

  SECTION("expected_liveness.json exists and is non-empty") {
    auto content = readFile(base + "expected_liveness.json");
    CHECK_FALSE(content.empty());
    // Basic sanity: contains our key marker strings
    CHECK(content.find("\"entry_points\"") != std::string::npos);
    CHECK(content.find("\"pessimistic\"") != std::string::npos);
    CHECK(content.find("\"optimistic\"") != std::string::npos);
  }
}

// ============================================================================
// CallGraph data structure unit tests (stubs — implement with CallGraph.h)
// ============================================================================

// TEST_CASE("CallGraph stores nodes and edges", "[dead_code][callgraph]") {
//   // CallGraph graph;
//
//   SECTION("empty graph has no nodes") {
//     // CHECK(graph.nodeCount() == 0);
//     // CHECK(graph.edgeCount() == 0);
//   }
//
//   SECTION("add nodes and query") {
//     // graph.addNode({"main", "main.cpp", 10, false, false, ""});
//     // graph.addNode({"foo", "foo.cpp", 5, false, false, ""});
//     // CHECK(graph.nodeCount() == 2);
//   }
//
//   SECTION("add edges and query adjacency") {
//     // graph.addNode({"main", "main.cpp", 10, true, false, ""});
//     // graph.addNode({"foo", "foo.cpp", 5, false, false, ""});
//     // graph.addEdge({"main", "foo", EdgeKind::DirectCall,
//     //                Confidence::Proven, "main.cpp:12:5", 0});
//     // auto callees = graph.calleesOf("main");
//     // REQUIRE(callees.size() == 1);
//     // CHECK(callees[0]->calleeName == "foo");
//   }
//
//   SECTION("virtual dispatch edges have Plausible confidence") {
//     // graph.addNode({"print_shape_info", "shapes.cpp", 80, false, false, ""});
//     // graph.addNode({"Circle::area", "shapes.cpp", 14, false, true, "Circle"});
//     // graph.addNode({"Square::area", "shapes.cpp", 54, false, true, "Square"});
//     // graph.addEdge({"print_shape_info", "Circle::area",
//     //                EdgeKind::VirtualDispatch, Confidence::Proven,
//     //                "shapes.cpp:82:20", 0});
//     // graph.addEdge({"print_shape_info", "Square::area",
//     //                EdgeKind::VirtualDispatch, Confidence::Plausible,
//     //                "shapes.cpp:82:20", 0});
//   }
// }

// ============================================================================
// Liveness propagation unit tests (stubs — implement with DeadCodeAnalyzer.h)
// ============================================================================

// TEST_CASE("Liveness propagation from entry points",
//           "[dead_code][liveness]") {
//
//   // Build a small hand-crafted graph:
//   //   main -> foo (Proven)
//   //   main -> bar (Plausible)
//   //   foo  -> baz (Proven)
//   //   (qux has no incoming edges)
//
//   SECTION("pessimistic mode: only proven paths") {
//     // main: Alive, foo: Alive, baz: Alive
//     // bar: Dead (only reachable via Plausible edge)
//     // qux: Dead
//   }
//
//   SECTION("optimistic mode: proven + plausible paths") {
//     // main: Alive, foo: Alive, baz: Alive
//     // bar: OptimisticallyAlive (reachable via Plausible edge)
//     // qux: Dead
//   }
//
//   SECTION("cascading plausibility") {
//     // main -> A (Proven) -> B (Plausible) -> C (Proven)
//     // Pessimistic: A alive, B dead, C dead
//     // Optimistic: A alive, B optimistically alive, C optimistically alive
//   }
// }

// ============================================================================
// Integration tests using in-memory compilation (stubs)
// ============================================================================

// TEST_CASE("CallGraphBuilder indexes direct calls",
//           "[dead_code][builder][integration]") {
//   // std::string code = R"(
//   //   void bar() {}
//   //   void foo() { bar(); }
//   //   int main() { foo(); return 0; }
//   // )";
//   //
//   // CallGraph graph;
//   // CallGraphBuilderFactory factory(graph);
//   // REQUIRE(clang::tooling::runToolOnCodeWithArgs(
//   //     factory.create(), code, {"-std=c++17"}, "test.cpp"));
//   //
//   // CHECK(graph.nodeCount() == 3);
//   // auto callees = graph.calleesOf("main");
//   // REQUIRE(callees.size() == 1);
//   // CHECK(callees[0]->calleeName == "foo");
// }

// TEST_CASE("CallGraphBuilder tracks virtual dispatch",
//           "[dead_code][builder][virtual]") {
//   // std::string code = R"(
//   //   struct Base {
//   //     virtual ~Base() = default;
//   //     virtual int value() const = 0;
//   //   };
//   //   struct Derived : Base {
//   //     int value() const override { return 42; }
//   //   };
//   //   int use(const Base& b) { return b.value(); }
//   //   int main() { Derived d; return use(d); }
//   // )";
//   //
//   // CallGraph graph;
//   // CallGraphBuilderFactory factory(graph);
//   // REQUIRE(clang::tooling::runToolOnCodeWithArgs(
//   //     factory.create(), code, {"-std=c++17"}, "test.cpp"));
//   //
//   // // use() should have edges to Base::value (Proven) and
//   // // Derived::value (Plausible or Proven depending on analysis)
// }

// TEST_CASE("CallGraphBuilder tracks function pointer indirection",
//           "[dead_code][builder][fnptr]") {
//   // std::string code = R"(
//   //   double transform(double x) { return x * 2; }
//   //   using Fn = double(*)(double);
//   //   double apply(Fn f, double x) { return f(x); }
//   //   int main() { return (int)apply(transform, 1.0); }
//   // )";
//   //
//   // CallGraph graph;
//   // CallGraphBuilderFactory factory(graph);
//   // REQUIRE(clang::tooling::runToolOnCodeWithArgs(
//   //     factory.create(), code, {"-std=c++17"}, "test.cpp"));
//   //
//   // // transform should be reachable from main through apply
// }

// ============================================================================
// Integration tests using example files (stubs)
// ============================================================================

// TEST_CASE("Dead code analysis on example project",
//           "[dead_code][integration][example]") {
//   // 1. Load compile_commands.json or create FixedCompilationDatabase
//   //    for examples/dead_code/
//   // 2. Run CallGraphBuilder on all .cpp files
//   // 3. Run DeadCodeAnalyzer with entry_points=["main"]
//   // 4. Load expected_liveness.json
//   // 5. For each function in expected, compare against analyzer results
//   //
//   // SECTION("pessimistic mode matches expected") { ... }
//   // SECTION("optimistic mode matches expected") { ... }
//   // SECTION("public_api config makes mean() alive") { ... }
// }

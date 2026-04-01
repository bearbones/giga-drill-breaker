#include "giga_drill/Analyzer.h"
#include "giga_drill/GlobalIndex.h"
#include "giga_drill/Indexer.h"

#include "clang/Tooling/CompilationDatabase.h"
#include "clang/Tooling/Tooling.h"

#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

using namespace giga_drill;

// ============================================================================
// GlobalIndex unit tests
// ============================================================================

TEST_CASE("GlobalIndex stores and retrieves function overloads",
          "[GlobalIndex]") {
  GlobalIndex index;

  SECTION("empty index returns no results") {
    auto results = index.findOverloads("MathLib::scale");
    CHECK(results.empty());
    CHECK(index.overloadCount() == 0);
  }

  SECTION("add and retrieve overloads") {
    index.addFunctionOverload({"MathLib::scale", "Core.hpp",
                               {"MathLib::Vector", "int"}, "void", 5});
    index.addFunctionOverload({"MathLib::scale", "Extension.hpp",
                               {"MathLib::Vector", "double"}, "void", 3});

    auto results = index.findOverloads("MathLib::scale");
    REQUIRE(results.size() == 2);
    CHECK(results[0]->headerPath == "Core.hpp");
    CHECK(results[1]->headerPath == "Extension.hpp");
    CHECK(index.overloadCount() == 2);
  }

  SECTION("different functions are separate") {
    index.addFunctionOverload(
        {"MathLib::scale", "Core.hpp", {"int"}, "void", 1});
    index.addFunctionOverload(
        {"MathLib::rotate", "Core.hpp", {"double"}, "void", 2});

    CHECK(index.findOverloads("MathLib::scale").size() == 1);
    CHECK(index.findOverloads("MathLib::rotate").size() == 1);
    CHECK(index.findOverloads("MathLib::translate").empty());
  }
}

TEST_CASE("GlobalIndex stores and retrieves deduction guides",
          "[GlobalIndex]") {
  GlobalIndex index;

  SECTION("empty index returns no results") {
    auto results = index.findDeductionGuides("Container");
    CHECK(results.empty());
    CHECK(index.guideCount() == 0);
  }

  SECTION("add and retrieve guides") {
    index.addDeductionGuide({"Container", "Guide.hpp", {"const char *"},
                             "Container<std::string>", 5});

    auto results = index.findDeductionGuides("Container");
    REQUIRE(results.size() == 1);
    CHECK(results[0]->templateName == "Container");
    CHECK(results[0]->deducedType == "Container<std::string>");
    CHECK(index.guideCount() == 1);
  }
}

// ============================================================================
// Indexer integration tests (using in-memory compilation)
// ============================================================================

TEST_CASE("Indexer collects function overloads from source",
          "[Indexer]") {
  GlobalIndex index;

  // Simple source with two overloads in a namespace.
  std::string code = R"(
    namespace MathLib {
      struct Vector {};
      void scale(Vector, int) {}
      void scale(Vector, double) {}
    }
  )";

  // Run the indexer on in-memory code.
  IndexerActionFactory factory(index);
  REQUIRE(clang::tooling::runToolOnCodeWithArgs(
      factory.create(), code, {"-std=c++17"}, "test_input.cpp"));

  auto overloads = index.findOverloads("MathLib::scale");
  REQUIRE(overloads.size() == 2);

  // Verify parameter types were extracted.
  bool hasInt = false, hasDouble = false;
  for (const auto *entry : overloads) {
    REQUIRE(entry->paramTypes.size() == 2);
    // Clang prints param types unqualified within their namespace context.
    CHECK((entry->paramTypes[0] == "Vector" ||
           entry->paramTypes[0] == "MathLib::Vector"));
    if (entry->paramTypes[1] == "int")
      hasInt = true;
    if (entry->paramTypes[1] == "double")
      hasDouble = true;
  }
  CHECK(hasInt);
  CHECK(hasDouble);
}

TEST_CASE("Indexer collects deduction guides from source",
          "[Indexer]") {
  GlobalIndex index;

  std::string code = R"(
    #include <string>

    template <typename T>
    struct Container {
      T value;
      Container(T v) : value(v) {}
    };

    Container(const char*) -> Container<std::string>;
  )";

  IndexerActionFactory factory(index);
  REQUIRE(clang::tooling::runToolOnCodeWithArgs(
      factory.create(), code, {"-std=c++17"}, "test_input.cpp"));

  auto guides = index.findDeductionGuides("Container");
  REQUIRE(guides.size() == 1);
  CHECK(guides[0]->templateName == "Container");
  // The deduced type should mention string.
  CHECK(guides[0]->deducedType.find("string") != std::string::npos);
}

TEST_CASE("Indexer skips implicit and compiler-generated declarations",
          "[Indexer]") {
  GlobalIndex index;

  // A struct with implicitly generated special member functions.
  std::string code = R"(
    struct Foo {
      int x;
    };
  )";

  IndexerActionFactory factory(index);
  REQUIRE(clang::tooling::runToolOnCodeWithArgs(
      factory.create(), code, {"-std=c++17"}, "test_input.cpp"));

  // Should not index implicit constructors, destructors, etc.
  CHECK(index.overloadCount() == 0);
}

// ============================================================================
// Analyzer integration tests — ADL
// ============================================================================

TEST_CASE("Analyzer detects fragile ADL when overload is invisible",
          "[Analyzer][ADL]") {
  // First, build a global index that knows about both overloads.
  // Note: Clang prints param types unqualified within namespace context,
  // so we use "Vector" not "MathLib::Vector" to match what the Indexer
  // would produce.
  GlobalIndex index;
  index.addFunctionOverload(
      {"MathLib::scale", "Core.hpp", {"Vector", "int"}, "void", 5});
  index.addFunctionOverload(
      {"MathLib::scale", "Extension.hpp", {"Vector", "double"}, "void", 3});

  // Now analyze code that only sees the int overload.
  std::string code = R"(
    namespace MathLib {
      struct Vector {};
      void scale(Vector, int) {}
    }
    void test() {
      MathLib::Vector v;
      scale(v, 3.14);
    }
  )";

  std::vector<Diagnostic> diagnostics;
  auto action = std::make_unique<AnalyzerAction>(index, diagnostics);
  REQUIRE(clang::tooling::runToolOnCodeWithArgs(
      std::move(action), code, {"-std=c++17"}, "test_input.cpp"));

  // Should detect that Extension.hpp has a scale(Vector, double) overload
  // that is not visible.
  REQUIRE(diagnostics.size() >= 1);
  CHECK(diagnostics[0].kind == Diagnostic::ADL_Fallback);
  CHECK(diagnostics[0].missingHeader == "Extension.hpp");
  CHECK(diagnostics[0].message.find("Extension.hpp") != std::string::npos);
}

TEST_CASE("Analyzer reports no diagnostic when all overloads are visible",
          "[Analyzer][ADL]") {
  GlobalIndex index;
  // Only one overload exists globally — no fragility.
  index.addFunctionOverload(
      {"MathLib::scale", "test_input.cpp", {"Vector", "int"}, "void", 5});

  std::string code = R"(
    namespace MathLib {
      struct Vector {};
      void scale(Vector, int) {}
    }
    void test() {
      MathLib::Vector v;
      scale(v, 42);
    }
  )";

  std::vector<Diagnostic> diagnostics;
  auto action = std::make_unique<AnalyzerAction>(index, diagnostics);
  REQUIRE(clang::tooling::runToolOnCodeWithArgs(
      std::move(action), code, {"-std=c++17"}, "test_input.cpp"));

  CHECK(diagnostics.empty());
}

TEST_CASE("Analyzer ignores explicitly qualified calls",
          "[Analyzer][ADL]") {
  GlobalIndex index;
  index.addFunctionOverload(
      {"MathLib::scale", "Core.hpp", {"Vector", "int"}, "void", 5});
  index.addFunctionOverload(
      {"MathLib::scale", "Extension.hpp", {"Vector", "double"}, "void", 3});

  // Explicitly qualified call — not an ADL concern.
  std::string code = R"(
    namespace MathLib {
      struct Vector {};
      void scale(Vector, int) {}
    }
    void test() {
      MathLib::Vector v;
      MathLib::scale(v, 3.14);
    }
  )";

  std::vector<Diagnostic> diagnostics;
  auto action = std::make_unique<AnalyzerAction>(index, diagnostics);
  REQUIRE(clang::tooling::runToolOnCodeWithArgs(
      std::move(action), code, {"-std=c++17"}, "test_input.cpp"));

  CHECK(diagnostics.empty());
}

// ============================================================================
// Analyzer integration tests — CTAD
// ============================================================================

TEST_CASE("Analyzer detects fragile CTAD when guide is invisible",
          "[Analyzer][CTAD]") {
  GlobalIndex index;
  index.addDeductionGuide({"Container", "Guide.hpp", {"const char *"},
                           "Container<std::string>", 5});

  // Code that uses CTAD without the explicit guide visible.
  std::string code = R"(
    template <typename T>
    struct Container {
      T value;
      Container(T v) : value(v) {}
    };

    void test() {
      Container c("Hello");
    }
  )";

  std::vector<Diagnostic> diagnostics;
  auto action = std::make_unique<AnalyzerAction>(index, diagnostics);
  REQUIRE(clang::tooling::runToolOnCodeWithArgs(
      std::move(action), code, {"-std=c++17"}, "test_input.cpp"));

  // Should detect that Guide.hpp has a deduction guide producing
  // Container<std::string> which isn't visible here.
  REQUIRE(diagnostics.size() >= 1);
  CHECK(diagnostics[0].kind == Diagnostic::CTAD_Fallback);
  CHECK(diagnostics[0].missingHeader == "Guide.hpp");
}

TEST_CASE("Analyzer reports no CTAD diagnostic when no guides exist",
          "[Analyzer][CTAD]") {
  GlobalIndex index;
  // No deduction guides in the global index.

  std::string code = R"(
    template <typename T>
    struct Container {
      T value;
      Container(T v) : value(v) {}
    };

    void test() {
      Container c(42);
    }
  )";

  std::vector<Diagnostic> diagnostics;
  auto action = std::make_unique<AnalyzerAction>(index, diagnostics);
  REQUIRE(clang::tooling::runToolOnCodeWithArgs(
      std::move(action), code, {"-std=c++17"}, "test_input.cpp"));

  CHECK(diagnostics.empty());
}

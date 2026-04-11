#include "giga_drill/mugann/Analyzer.h"
#include "giga_drill/mugann/GlobalIndex.h"
#include "giga_drill/mugann/Indexer.h"

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

  // Avoid #include <string> which requires system headers that may not
  // be available to the in-memory Clang tooling when built from source.
  std::string code = R"(
    struct MyString {
      const char *data;
      MyString(const char *s) : data(s) {}
    };

    template <typename T>
    struct Container {
      T value;
      Container(T v) : value(v) {}
    };

    Container(const char*) -> Container<MyString>;
  )";

  IndexerActionFactory factory(index);
  REQUIRE(clang::tooling::runToolOnCodeWithArgs(
      factory.create(), code, {"-std=c++17"}, "test_input.cpp"));

  auto guides = index.findDeductionGuides("Container");
  REQUIRE(guides.size() == 1);
  CHECK(guides[0]->templateName == "Container");
  // The deduced type should mention MyString.
  CHECK(guides[0]->deducedType.find("MyString") != std::string::npos);
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

TEST_CASE(
    "Analyzer does not warn when resolved operator overload is most specific",
    "[Analyzer][ADL][operator]") {
  // Reproduces the math-library false positive: the call is
  // float * Vector3 and the resolved operator*(float, const Vector3&) is the
  // most specific overload in the index. Unrelated overloads for Vector3D /
  // Matrix3 / double should not produce any warnings.
  GlobalIndex index;
  index.addFunctionOverload(
      {"MathLib::operator*", "Core.hpp", {"float", "const Vector3 &"},
       "Vector3", 10});
  index.addFunctionOverload(
      {"MathLib::operator*", "Precision.hpp",
       {"double", "const Vector3D &"}, "Vector3D", 20});
  index.addFunctionOverload(
      {"MathLib::operator*", "Matrix.hpp",
       {"const Matrix3 &", "const Matrix3 &"}, "Matrix3", 30});
  index.addFunctionOverload(
      {"MathLib::operator*", "Matrix.hpp",
       {"const Matrix3 &", "const Vector3 &"}, "Vector3", 45});

  std::string code = R"(
    namespace MathLib {
      struct Vector3 { float x, y, z; };
      Vector3 operator*(float f, const Vector3 &v) {
        return Vector3{v.x * f, v.y * f, v.z * f};
      }
    }
    void test() {
      float f = 2.0f;
      MathLib::Vector3 v3;
      auto result = f * v3;
      (void)result;
    }
  )";

  std::vector<Diagnostic> diagnostics;
  auto action = std::make_unique<AnalyzerAction>(index, diagnostics);
  REQUIRE(clang::tooling::runToolOnCodeWithArgs(
      std::move(action), code, {"-std=c++17"}, "test_input.cpp"));

  CHECK(diagnostics.empty());
}

TEST_CASE("Analyzer does not warn when call matches int overload exactly",
          "[Analyzer][ADL]") {
  // Narrowing-direction guard: the visible scale(Vector, int) is an exact
  // match for scale(v, 42). The invisible scale(Vector, double) would be a
  // worse match and should not be flagged.
  GlobalIndex index;
  index.addFunctionOverload(
      {"MathLib::scale", "Core.hpp", {"Vector", "int"}, "void", 5});
  index.addFunctionOverload(
      {"MathLib::scale", "Extension.hpp", {"Vector", "double"}, "void", 3});

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

TEST_CASE(
    "Analyzer warns about potential ambiguity between incomparable overloads",
    "[Analyzer][ADL][ambiguity]") {
  // pick(Vector, int, double) is visible; pick(Vector, double, int) is
  // invisible. The call pick(v, 1, 2) with two int numeric arguments is
  // unambiguous right now (only the first overload is visible) but
  // including CoreB.hpp would make the resolution ambiguous: each overload
  // wins on exactly one numeric parameter position. The Vector argument
  // exists only to trigger ADL into the MathLib namespace.
  GlobalIndex index;
  index.addFunctionOverload(
      {"MathLib::pick", "CoreA.hpp", {"Vector", "int", "double"}, "void", 5});
  index.addFunctionOverload(
      {"MathLib::pick", "CoreB.hpp", {"Vector", "double", "int"}, "void", 7});

  std::string code = R"(
    namespace MathLib {
      struct Vector {};
      void pick(Vector, int, double) {}
    }
    void test() {
      MathLib::Vector v;
      pick(v, 1, 2);
    }
  )";

  std::vector<Diagnostic> diagnostics;
  auto action = std::make_unique<AnalyzerAction>(index, diagnostics);
  REQUIRE(clang::tooling::runToolOnCodeWithArgs(
      std::move(action), code, {"-std=c++17"}, "test_input.cpp"));

  REQUIRE(diagnostics.size() == 1);
  CHECK(diagnostics[0].kind == Diagnostic::ADL_Ambiguity);
  CHECK(diagnostics[0].missingHeader == "CoreB.hpp");
  CHECK(diagnostics[0].message.find("ambiguous") != std::string::npos);
}

TEST_CASE("Analyzer does not warn about ambiguity for unrelated overloads",
          "[Analyzer][ADL][ambiguity]") {
  // Same unrelated operator* overloads as the "most specific" test, but
  // this one asserts that the ambiguity branch also stays silent.
  GlobalIndex index;
  index.addFunctionOverload(
      {"MathLib::operator*", "Core.hpp", {"float", "const Vector3 &"},
       "Vector3", 10});
  index.addFunctionOverload(
      {"MathLib::operator*", "Precision.hpp",
       {"double", "const Vector3D &"}, "Vector3D", 20});
  index.addFunctionOverload(
      {"MathLib::operator*", "Matrix.hpp",
       {"const Matrix3 &", "const Matrix3 &"}, "Matrix3", 30});

  std::string code = R"(
    namespace MathLib {
      struct Vector3 { float x, y, z; };
      Vector3 operator*(float f, const Vector3 &v) {
        return Vector3{v.x * f, v.y * f, v.z * f};
      }
    }
    void test() {
      float f = 2.0f;
      MathLib::Vector3 v3;
      auto result = f * v3;
      (void)result;
    }
  )";

  std::vector<Diagnostic> diagnostics;
  auto action = std::make_unique<AnalyzerAction>(index, diagnostics);
  REQUIRE(clang::tooling::runToolOnCodeWithArgs(
      std::move(action), code, {"-std=c++17"}, "test_input.cpp"));

  for (const auto &d : diagnostics) {
    CHECK(d.kind != Diagnostic::ADL_Ambiguity);
  }
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

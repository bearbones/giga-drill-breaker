#include "giga_drill/MatcherEngine.h"
#include "giga_drill/TransformPipeline.h"

#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <sstream>
#include <string>

// Helpers to load example files for comparison.
static std::string readFile(const std::string &path) {
  std::ifstream in(path);
  if (!in.is_open())
    return {};
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

// These integration tests are initially set up as TDD stubs.
// They define the expected behavior but the transform callbacks are not yet
// implemented, so they are expected to fail until the callbacks are written.

TEST_CASE("Example files are loadable", "[transforms][smoke]") {
  // Verify that the example files exist and can be read.
  // Paths are relative to the project root; tests should be run from there.
  SECTION("macro_split examples exist") {
    auto input = readFile(std::string(PROJECT_SOURCE_DIR) + "/examples/macro_split/input.cpp");
    auto expected = readFile(std::string(PROJECT_SOURCE_DIR) + "/examples/macro_split/expected.cpp");
    CHECK_FALSE(input.empty());
    CHECK_FALSE(expected.empty());
    CHECK(input != expected);
  }

  SECTION("builder_to_struct examples exist") {
    auto input = readFile(std::string(PROJECT_SOURCE_DIR) + "/examples/builder_to_struct/input.cpp");
    auto expected = readFile(std::string(PROJECT_SOURCE_DIR) + "/examples/builder_to_struct/expected.cpp");
    CHECK_FALSE(input.empty());
    CHECK_FALSE(expected.empty());
    CHECK(input != expected);
  }
}

// Placeholder for future integration tests that will run actual transforms
// on the example inputs and compare against expected outputs.
//
// TEST_CASE("Macro split transform", "[transforms][macro_split]") {
//   // 1. Load examples/macro_split/input.cpp
//   // 2. Set up transform rules for boolean macro splitting
//   // 3. Run MatcherEngine on the input
//   // 4. Apply replacements
//   // 5. Compare result to examples/macro_split/expected.cpp
// }
//
// TEST_CASE("Builder to struct transform", "[transforms][builder_to_struct]") {
//   // 1. Load examples/builder_to_struct/input.cpp
//   // 2. Set up transform rules for builder-to-struct conversion
//   // 3. Run MatcherEngine on the input
//   // 4. Apply replacements
//   // 5. Compare result to examples/builder_to_struct/expected.cpp
// }

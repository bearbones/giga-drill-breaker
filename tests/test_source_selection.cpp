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

// TU selection for the megascope bake (vycor/cli/SourceSelection.h).

#include "vycor/cli/SourceSelection.h"

#include "llvm/ADT/SmallString.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/raw_ostream.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <sstream>

using namespace vycor;

namespace {

/// A compilation database whose only job is to answer getAllFiles().
class FilesDb : public clang::tooling::CompilationDatabase {
public:
  explicit FilesDb(std::vector<std::string> files)
      : files_(std::move(files)) {}
  std::vector<clang::tooling::CompileCommand>
  getCompileCommands(llvm::StringRef) const override {
    return {};
  }
  std::vector<std::string> getAllFiles() const override { return files_; }

private:
  std::vector<std::string> files_;
};

const std::vector<std::string> kDb = {
    "/proj/Network/src/a.cpp", "/proj/Network/tests/a_test.cpp",
    "/proj/ThirdParty/zlib/z.c", "/proj/Core/b.cpp",
    "/proj/Network/src/a.cpp", // duplicate entry (multi-config databases)
};

std::vector<std::string> select(const SourceSelection &sel,
                                const std::string &stdinText = "",
                                SourceSelectionStats *stats = nullptr) {
  FilesDb db(kDb);
  std::istringstream in(stdinText);
  auto r = selectSources(db, sel, in, stats);
  REQUIRE(bool(r));
  return *r;
}

std::string writeTempList(llvm::StringRef body) {
  llvm::SmallString<128> p;
  llvm::sys::fs::createUniquePath("vycor-source-list-%%%%%%.txt", p, true);
  std::error_code ec;
  llvm::raw_fd_ostream os(p, ec);
  REQUIRE_FALSE(ec);
  os << body;
  return p.str().str();
}

} // namespace

TEST_CASE("no --source means every database entry, deduplicated",
          "[source-selection]") {
  SourceSelectionStats stats;
  auto files = select({}, "", &stats);
  CHECK(files == std::vector<std::string>{"/proj/Network/src/a.cpp",
                                          "/proj/Network/tests/a_test.cpp",
                                          "/proj/ThirdParty/zlib/z.c",
                                          "/proj/Core/b.cpp"});
  CHECK(stats.base == 4);
  CHECK(std::string(stats.baseSource) == "database");
}

TEST_CASE("--source-re and --skip-paths narrow the set", "[source-selection]") {
  SourceSelection sel;
  sel.regex = "/Network/";
  SourceSelectionStats stats;
  auto files = select(sel, "", &stats);
  CHECK(files == std::vector<std::string>{"/proj/Network/src/a.cpp",
                                          "/proj/Network/tests/a_test.cpp"});
  CHECK(stats.regexDropped == 2);

  sel.skipPaths = {"Network/tests"};
  files = select(sel, "", &stats);
  CHECK(files == std::vector<std::string>{"/proj/Network/src/a.cpp"});
  CHECK(stats.skipDropped == 1);

  SECTION("an invalid regex is an error, not an empty set") {
    FilesDb db(kDb);
    std::istringstream in;
    SourceSelection bad;
    bad.regex = "(unclosed";
    auto r = selectSources(db, bad, in);
    REQUIRE_FALSE(bool(r));
    CHECK(llvm::toString(r.takeError()).find("invalid --source-re") !=
          std::string::npos);
  }
}

TEST_CASE("explicit files and list files are unioned and filtered",
          "[source-selection]") {
  SourceSelection sel;
  sel.explicitFiles = {"/x/one.cpp", "/x/two.cpp"};

  SECTION("explicit only: the database is not consulted") {
    SourceSelectionStats stats;
    CHECK(select(sel, "", &stats) == sel.explicitFiles);
    CHECK(std::string(stats.baseSource) == "source");
  }

  SECTION("--source-list from a file, comments and blanks ignored") {
    std::string list =
        writeTempList("# header\n/x/two.cpp\n\n  /x/three.cpp  \n");
    sel.listFile = list;
    auto files = select(sel);
    std::remove(list.c_str());
    CHECK(files == std::vector<std::string>{"/x/one.cpp", "/x/two.cpp",
                                            "/x/three.cpp"});
  }

  SECTION("--source-list - reads stdin") {
    SourceSelection fromStdin;
    fromStdin.listFile = "-";
    fromStdin.regex = "two|three";
    auto files = select(fromStdin, "/x/one.cpp\n/x/two.cpp\n/x/three.cpp\n");
    CHECK(files == std::vector<std::string>{"/x/two.cpp", "/x/three.cpp"});
  }

  SECTION("a missing list file is an error") {
    FilesDb db(kDb);
    std::istringstream in;
    SourceSelection missing;
    missing.listFile = "/nonexistent/list.txt";
    auto r = selectSources(db, missing, in);
    REQUIRE_FALSE(bool(r));
    CHECK(llvm::toString(r.takeError()).find("cannot read --source-list") !=
          std::string::npos);
  }
}

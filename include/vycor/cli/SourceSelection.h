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

#pragma once

#include "clang/Tooling/CompilationDatabase.h"
#include "llvm/Support/Error.h"

#include <istream>
#include <string>
#include <vector>

// ============================================================================
// TU selection for the bake (docs/megascope-cli-review.md §2.4). Replaces
// the "write a Python snippet over compile_commands.json" step:
//
//   (no --source)        every entry of the compilation database
//   --source F ...       explicit files
//   --source-list FILE   one path per line ("-" = stdin; '#' comments)
//   --source-re REGEX    keep only paths matching (POSIX ERE, searched)
//   --skip-paths P ...   drop paths under a directory component (the
//                        existing CollapseFilter semantics)
//
// Explicit files and list entries are unioned (in order, deduplicated);
// when neither is given the database is the base set. The regex and the
// skip filter then narrow whatever the base set is.
// ============================================================================

namespace vycor {

struct SourceSelection {
  std::vector<std::string> explicitFiles; // --source
  std::string listFile;                   // --source-list ("-" = stdin)
  std::string regex;                      // --source-re
  std::vector<std::string> skipPaths;     // --skip-paths
};

struct SourceSelectionStats {
  size_t base = 0;        // candidates before filtering
  size_t regexDropped = 0;
  size_t skipDropped = 0;
  const char *baseSource = ""; // "database", "source", "source-list", ...
};

/// Resolve `sel` against `db`. `stdinFor` backs `--source-list -`.
/// Errors: unreadable list file, invalid regex. An empty result is not an
/// error here (the caller decides what "nothing to index" means).
llvm::Expected<std::vector<std::string>>
selectSources(const clang::tooling::CompilationDatabase &db,
              const SourceSelection &sel, std::istream &stdinFor,
              SourceSelectionStats *stats = nullptr);

} // namespace vycor

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

#include "vycor/cli/SourceSelection.h"
#include "vycor/callgraph/CollapseFilter.h"

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Regex.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <unordered_set>

namespace vycor {

namespace {

llvm::Error selectionError(const llvm::Twine &msg) {
  return llvm::createStringError(llvm::inconvertibleErrorCode(), msg);
}

void appendUnique(std::vector<std::string> &out,
                  std::unordered_set<std::string> &seen, std::string path) {
  if (seen.insert(path).second)
    out.push_back(std::move(path));
}

/// One path per line; blank lines and '#' comments ignored; surrounding
/// whitespace trimmed.
void appendListLines(std::istream &in, std::vector<std::string> &out,
                     std::unordered_set<std::string> &seen) {
  std::string line;
  while (std::getline(in, line)) {
    llvm::StringRef s = llvm::StringRef(line).trim();
    if (s.empty() || s.starts_with("#"))
      continue;
    appendUnique(out, seen, s.str());
  }
}

} // namespace

llvm::Expected<std::vector<std::string>>
selectSources(const clang::tooling::CompilationDatabase &db,
              const SourceSelection &sel, std::istream &stdinFor,
              SourceSelectionStats *stats) {
  std::vector<std::string> files;
  std::unordered_set<std::string> seen;
  const char *baseSource = "database";

  for (const auto &f : sel.explicitFiles)
    appendUnique(files, seen, f);
  if (!sel.listFile.empty()) {
    if (sel.listFile == "-") {
      appendListLines(stdinFor, files, seen);
    } else {
      std::ifstream in(sel.listFile);
      if (!in)
        return selectionError("cannot read --source-list " + sel.listFile);
      appendListLines(in, files, seen);
    }
  }
  if (!sel.explicitFiles.empty() && !sel.listFile.empty())
    baseSource = "source + source-list";
  else if (!sel.explicitFiles.empty())
    baseSource = "source";
  else if (!sel.listFile.empty())
    baseSource = "source-list";
  else
    for (const auto &f : db.getAllFiles())
      appendUnique(files, seen, f);

  if (stats) {
    stats->base = files.size();
    stats->baseSource = baseSource;
  }

  if (!sel.regex.empty()) {
    llvm::Regex re(sel.regex);
    std::string reError;
    if (!re.isValid(reError))
      return selectionError("invalid --source-re '" + sel.regex + "': " +
                            reError);
    size_t before = files.size();
    files.erase(std::remove_if(files.begin(), files.end(),
                               [&](const std::string &f) {
                                 return !re.match(f);
                               }),
                files.end());
    if (stats)
      stats->regexDropped = before - files.size();
  }

  if (!sel.skipPaths.empty()) {
    CollapseFilter skip(sel.skipPaths);
    size_t before = files.size();
    files.erase(std::remove_if(files.begin(), files.end(),
                               [&](const std::string &f) {
                                 return skip.isCollapsed(f);
                               }),
                files.end());
    if (stats)
      stats->skipDropped = before - files.size();
  }
  return files;
}

} // namespace vycor

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

#include "giga_drill/lagann/TransformPipeline.h"

#include "clang/Tooling/CompilationDatabase.h"
#include "clang/Tooling/Refactoring.h"
#include "llvm/Support/raw_ostream.h"

namespace giga_drill {

void TransformPipeline::addPass(std::vector<TransformRule> rules) {
  passes_.push_back(std::move(rules));
}

int TransformPipeline::execute(const std::string &buildPath,
                               const std::vector<std::string> &sourceFiles,
                               bool dryRun) {
  std::string dbLoadError;
  auto compDb =
      clang::tooling::CompilationDatabase::loadFromDirectory(buildPath,
                                                             dbLoadError);
  if (!compDb) {
    llvm::errs() << "Error loading compilation database from " << buildPath
                 << ": " << dbLoadError << "\n";
    return 1;
  }

  for (auto &passRules : passes_) {
    MatcherEngine engine;
    for (auto &rule : passRules) {
      std::string error;
      if (!engine.addRule(rule, error)) {
        llvm::errs() << "Error adding rule: " << error << "\n";
        return 1;
      }
    }

    if (int ret = engine.run(*compDb, sourceFiles))
      return ret;

    // Merge replacements from this pass into the overall set.
    for (auto &[file, repls] : engine.getReplacements()) {
      for (auto &r : repls) {
        if (auto err = allReplacements_[file].add(r)) {
          llvm::errs() << "Replacement merge conflict: "
                       << llvm::toString(std::move(err)) << "\n";
        }
      }
    }

    // TODO: apply replacements to files between passes when !dryRun
    // For now, replacements are just collected.
  }

  if (!dryRun) {
    // TODO: write replacements to disk
    llvm::outs() << "Collected " << allReplacements_.size()
                 << " file(s) with replacements.\n";
  }

  return 0;
}

const std::map<std::string, clang::tooling::Replacements> &
TransformPipeline::getReplacements() const {
  return allReplacements_;
}

} // namespace giga_drill

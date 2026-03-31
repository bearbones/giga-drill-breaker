#pragma once

#include "giga_drill/MatcherEngine.h"
#include <string>
#include <vector>

namespace giga_drill {

// Orchestrates multiple passes of transform rules over a set of source files.
// Each pass runs a MatcherEngine with its own set of rules, collects
// replacements, and (optionally) applies them before the next pass.
class TransformPipeline {
public:
  // Add a pass consisting of one or more transform rules.
  void addPass(std::vector<TransformRule> rules);

  // Execute all passes sequentially against the given source files.
  // If dryRun is true, replacements are collected but not written to disk.
  // Returns 0 on success, nonzero on failure.
  int execute(const std::string &buildPath,
              const std::vector<std::string> &sourceFiles, bool dryRun = false);

  // Retrieve all accumulated replacements across all passes, keyed by file.
  const std::map<std::string, clang::tooling::Replacements> &
  getReplacements() const;

private:
  std::vector<std::vector<TransformRule>> passes_;
  std::map<std::string, clang::tooling::Replacements> allReplacements_;
};

} // namespace giga_drill

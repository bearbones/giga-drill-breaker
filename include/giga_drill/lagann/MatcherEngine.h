// Copyright (c) 2026 The giga-drill-breaker Authors
// Original author: Alex Mason
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/ASTMatchers/Dynamic/Parser.h"
#include "clang/Tooling/Core/Replacement.h"
#include "clang/Tooling/Tooling.h"

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace giga_drill {

// Callback invoked when a matcher fires. Receives the match result and returns
// zero or more source replacements to apply.
using ReplacementCallback = std::function<std::vector<clang::tooling::Replacement>(
    const clang::ast_matchers::MatchFinder::MatchResult &)>;

// A single transform rule: a matcher expression string, a bind ID for the
// root matched node, and a callback that produces replacements.
struct TransformRule {
  std::string matcherExpression;
  std::string bindId;
  ReplacementCallback callback;
};

// Parses AST matcher expressions from strings and runs them against source
// files, collecting replacements produced by registered callbacks.
class MatcherEngine {
public:
  MatcherEngine();
  ~MatcherEngine();

  // Parse a dynamic matcher expression string. Returns the parsed matcher on
  // success, or std::nullopt with a diagnostic written to errorOut.
  static std::optional<clang::ast_matchers::internal::DynTypedMatcher>
  parse(const std::string &matcherExpression, std::string &errorOut);

  // Register a transform rule. Parses the matcher expression, binds it, and
  // adds it to the internal MatchFinder. Returns false on parse failure.
  bool addRule(const TransformRule &rule, std::string &errorOut);

  // Run all registered matchers against the given source files.
  // Returns 0 on success, nonzero on tool failure.
  int run(const clang::tooling::CompilationDatabase &compDb,
          const std::vector<std::string> &sourceFiles);

  // Retrieve the accumulated replacements, keyed by file path.
  const std::map<std::string, clang::tooling::Replacements> &
  getReplacements() const;

private:
  clang::ast_matchers::MatchFinder finder_;
  std::map<std::string, clang::tooling::Replacements> replacements_;
  std::vector<
      std::unique_ptr<clang::ast_matchers::MatchFinder::MatchCallback>>
      callbacks_;
};

} // namespace giga_drill

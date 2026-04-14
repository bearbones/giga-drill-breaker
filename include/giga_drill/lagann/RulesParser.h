#pragma once

#include "giga_drill/lagann/MatcherEngine.h"

#include "llvm/Support/Error.h"
#include "llvm/Support/JSON.h"

#include <string>
#include <vector>

namespace giga_drill {

// ---------------------------------------------------------------------------
// JSON Schema Types
// ---------------------------------------------------------------------------

struct JsonFindSpec {
  std::string matcher;
};

struct JsonReplaceSpec {
  std::string target;
  std::string scope;     // "node" or "macro-expansion", defaults to "node"
  std::string withTempl; // template string
  std::vector<std::string> macroNames; // for macro-expansion: only match these
  bool argOnly = true; // for macro-expansion: require matched node to be a
                       // macro argument (not a body token). Set false when
                       // matching structural patterns introduced by the macro.
};

struct JsonRule {
  JsonFindSpec find;
  JsonReplaceSpec replace;
};

struct JsonPass {
  std::string name;    // optional human-readable name
  std::string context; // optional context matcher expression
  std::vector<JsonRule> rules;
};

struct JsonRulesFile {
  std::vector<JsonPass> passes;
};

// JSON deserialization
bool fromJSON(const llvm::json::Value &value, JsonFindSpec &out,
              llvm::json::Path path);
bool fromJSON(const llvm::json::Value &value, JsonReplaceSpec &out,
              llvm::json::Path path);
bool fromJSON(const llvm::json::Value &value, JsonRule &out,
              llvm::json::Path path);
bool fromJSON(const llvm::json::Value &value, JsonPass &out,
              llvm::json::Path path);
bool fromJSON(const llvm::json::Value &value, JsonRulesFile &out,
              llvm::json::Path path);

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

/// Parse a JSON rules file from disk.
llvm::Expected<JsonRulesFile> parseRulesFile(llvm::StringRef path);

/// Convert parsed JSON rules into TransformPipeline passes.
/// Each inner vector is one pass of TransformRules.
llvm::Expected<std::vector<std::vector<TransformRule>>>
buildPipeline(const JsonRulesFile &rules);

} // namespace giga_drill

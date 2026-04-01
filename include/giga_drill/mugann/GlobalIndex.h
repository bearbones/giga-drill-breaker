#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace giga_drill {

// A single function overload discovered during indexing.
struct FunctionOverloadEntry {
  std::string qualifiedName; // e.g. "MathLib::scale"
  std::string headerPath;   // file where declared
  std::vector<std::string> paramTypes; // e.g. {"MathLib::Vector", "double"}
  std::string returnType;
  unsigned sourceLine = 0;
};

// A single deduction guide discovered during indexing.
struct DeductionGuideEntry {
  std::string templateName;            // e.g. "Container"
  std::string headerPath;             // file where declared
  std::vector<std::string> paramTypes; // e.g. {"const char *"}
  std::string deducedType;            // e.g. "Container<std::string>"
  unsigned sourceLine = 0;
};

// A diagnostic emitted when shadow lookup finds a mismatch.
struct Diagnostic {
  enum Kind { ADL_Fallback, CTAD_Fallback };
  Kind kind;
  std::string callLocation;  // file:line:col of the call site
  std::string resolvedDecl;  // what the compiler chose
  std::string betterDecl;    // what the global index found
  std::string missingHeader; // which header to include
  std::string message;       // human-readable diagnostic
};

// Project-wide database of all function overloads and deduction guides.
class GlobalIndex {
public:
  void addFunctionOverload(FunctionOverloadEntry entry);
  void addDeductionGuide(DeductionGuideEntry entry);

  // Find all overloads for a given qualified function name.
  std::vector<const FunctionOverloadEntry *>
  findOverloads(const std::string &qualifiedName) const;

  // Find all deduction guides for a given template name.
  std::vector<const DeductionGuideEntry *>
  findDeductionGuides(const std::string &templateName) const;

  // Total counts for testing/debugging.
  size_t overloadCount() const;
  size_t guideCount() const;

private:
  std::unordered_map<std::string, std::vector<FunctionOverloadEntry>> overloads_;
  std::unordered_map<std::string, std::vector<DeductionGuideEntry>> guides_;
};

} // namespace giga_drill

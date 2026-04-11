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

// Coverage-relevant properties of a header-defined class member function.
// Used to diagnose why certain inline methods get dummy coverage records
// (hash 0x0) while sibling methods in the same class do not.
struct CoveragePropertyEntry {
  std::string qualifiedName;    // e.g. "MyClass::getValue"
  std::string headerPath;
  unsigned sourceLine = 0;
  std::string enclosingClass;   // qualified name of parent CXXRecordDecl

  // GVA linkage — the primary signal for coverage instrumentation fate.
  // Maps to clang::GVALinkage: 0=Internal, 1=AvailableExternally,
  // 2=DiscardableODR, 3=StrongODR, 4=StrongExternal
  int gvaLinkage = -1;

  bool isInlined = false;
  bool isConstexpr = false;
  bool isDefaulted = false;
  bool isTrivial = false;
  bool isVirtual = false;
  bool isStaticMethod = false;
  bool isImplicitlyInstantiable = false;
  int templatedKind = 0;
  int storageClass = 0;
  int formalLinkage = 0;
  unsigned bodyStmtCount = 0;   // body complexity heuristic
  std::string signature;        // e.g. "int MyClass::getValue() const"
};

// A diagnostic emitted when analysis finds an issue.
struct Diagnostic {
  enum Kind {
    ADL_Fallback,
    ADL_Ambiguity,                // including the missing header would make
                                  // this call an ambiguous overload resolution
    CTAD_Fallback,
    Coverage_GVAMismatch,         // siblings have different GVA linkage
    Coverage_DiscardableODR,      // method has GVA_DiscardableODR (COMDAT risk)
    Coverage_AvailableExternally, // method may be discarded by optimizer
    Coverage_PropertyDivergence,  // siblings diverge on complexity/properties
    DeadCode_Pessimistic,         // function unreachable via proven paths
    DeadCode_Optimistic,          // function reachable only via plausible paths
  };
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

  // Coverage property tracking.
  void addCoverageProperty(CoveragePropertyEntry entry);

  std::vector<const CoveragePropertyEntry *>
  findClassMethods(const std::string &enclosingClass) const;

  std::vector<std::string> allIndexedClasses() const;

  // Total counts for testing/debugging.
  size_t overloadCount() const;
  size_t guideCount() const;
  size_t coverageEntryCount() const;

private:
  std::unordered_map<std::string, std::vector<FunctionOverloadEntry>> overloads_;
  std::unordered_map<std::string, std::vector<DeductionGuideEntry>> guides_;
  std::unordered_map<std::string, std::vector<CoveragePropertyEntry>>
      coverageProps_;
};

} // namespace giga_drill

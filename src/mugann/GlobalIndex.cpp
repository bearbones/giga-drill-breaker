// Copyright (c) 2026 The giga-drill-breaker Authors
// Original author: Alex Mason
//
// SPDX-License-Identifier: Apache-2.0

#include "giga_drill/mugann/GlobalIndex.h"
#include "giga_drill/mugann/TypeNormalize.h"

#include "llvm/ADT/StringSwitch.h"

#include <algorithm>
#include <unordered_set>

namespace giga_drill {

namespace {

// The fixed set of builtin arithmetic type names (names as produced by
// clang::QualType::getAsString() after normalization). Any two normalized
// type names in this set are treated as mutually convertible, matching the
// behaviour of C++ standard conversions.
bool isArithmeticTypeName(const std::string &normalized) {
  return llvm::StringSwitch<bool>(normalized)
      .Case("bool", true)
      .Case("char", true)
      .Case("signedchar", true)
      .Case("unsignedchar", true)
      .Case("wchar_t", true)
      .Case("char8_t", true)
      .Case("char16_t", true)
      .Case("char32_t", true)
      .Case("short", true)
      .Case("shortint", true)
      .Case("unsignedshort", true)
      .Case("unsignedshortint", true)
      .Case("int", true)
      .Case("unsigned", true)
      .Case("unsignedint", true)
      .Case("long", true)
      .Case("longint", true)
      .Case("unsignedlong", true)
      .Case("unsignedlongint", true)
      .Case("longlong", true)
      .Case("longlongint", true)
      .Case("unsignedlonglong", true)
      .Case("unsignedlonglongint", true)
      .Case("float", true)
      .Case("double", true)
      .Case("longdouble", true)
      .Default(false);
}

// Strip a single trailing '*' to compare pointer-to-class against base.
// Reference qualifiers are already stripped by normalizeTypeForMatching.
std::string stripPointer(const std::string &s) {
  if (!s.empty() && s.back() == '*') {
    std::string out = s;
    out.pop_back();
    return out;
  }
  return s;
}

} // namespace

void TypeRelationIndex::addBase(std::string derived, std::string base) {
  auto &v = bases[std::move(derived)];
  if (std::find(v.begin(), v.end(), base) == v.end())
    v.push_back(std::move(base));
}

void TypeRelationIndex::addCtorEdge(std::string toType, std::string fromType) {
  auto &v = ctorEdges[std::move(toType)];
  if (std::find(v.begin(), v.end(), fromType) == v.end())
    v.push_back(std::move(fromType));
}

void TypeRelationIndex::addConvOpEdge(std::string fromType,
                                      std::string toType) {
  auto &v = convOpEdges[std::move(fromType)];
  if (std::find(v.begin(), v.end(), toType) == v.end())
    v.push_back(std::move(toType));
}

bool TypeRelationIndex::isBaseOrSelf(const std::string &derived,
                                     const std::string &maybeBase) const {
  if (derived == maybeBase)
    return true;
  std::unordered_set<std::string> seen;
  std::vector<std::string> stack{derived};
  while (!stack.empty()) {
    std::string cur = std::move(stack.back());
    stack.pop_back();
    if (!seen.insert(cur).second)
      continue;
    auto it = bases.find(cur);
    if (it == bases.end())
      continue;
    for (const auto &b : it->second) {
      if (b == maybeBase)
        return true;
      stack.push_back(b);
    }
  }
  return false;
}

bool TypeRelationIndex::isConvertible(const std::string &from,
                                      const std::string &to) const {
  // Identity after normalization.
  if (from == to)
    return true;

  // Builtin arithmetic conversions.
  if (isArithmeticTypeName(from) && isArithmeticTypeName(to))
    return true;

  // Derived-to-base via references or pointers. normalizeTypeForMatching
  // already removed '&'; we still need to strip a trailing '*' symmetrically
  // so that `Derived*` satisfies a `Base*` parameter.
  {
    std::string fromClass = stripPointer(from);
    std::string toClass = stripPointer(to);
    if (fromClass != from || toClass != to) {
      // Both sides had a pointer; must strip in lockstep.
      if (fromClass != from && toClass != to &&
          isBaseOrSelf(fromClass, toClass))
        return true;
    } else if (isBaseOrSelf(fromClass, toClass) && fromClass != toClass) {
      // Reference/value case: derived value can bind to base reference.
      return true;
    }
  }

  // Single-hop non-explicit converting constructor on the target type.
  {
    auto it = ctorEdges.find(to);
    if (it != ctorEdges.end()) {
      for (const auto &src : it->second)
        if (src == from)
          return true;
    }
  }

  // Single-hop non-explicit conversion operator on the source type.
  {
    auto it = convOpEdges.find(from);
    if (it != convOpEdges.end()) {
      for (const auto &tgt : it->second)
        if (tgt == to)
          return true;
    }
  }

  return false;
}

void GlobalIndex::addFunctionOverload(FunctionOverloadEntry entry) {
  overloads_[entry.qualifiedName].push_back(std::move(entry));
}

void GlobalIndex::addDeductionGuide(DeductionGuideEntry entry) {
  guides_[entry.templateName].push_back(std::move(entry));
}

std::vector<const FunctionOverloadEntry *>
GlobalIndex::findOverloads(const std::string &qualifiedName) const {
  std::vector<const FunctionOverloadEntry *> result;
  auto it = overloads_.find(qualifiedName);
  if (it != overloads_.end()) {
    for (const auto &entry : it->second)
      result.push_back(&entry);
  }
  return result;
}

std::vector<const DeductionGuideEntry *>
GlobalIndex::findDeductionGuides(const std::string &templateName) const {
  std::vector<const DeductionGuideEntry *> result;
  auto it = guides_.find(templateName);
  if (it != guides_.end()) {
    for (const auto &entry : it->second)
      result.push_back(&entry);
  }
  return result;
}

size_t GlobalIndex::overloadCount() const {
  size_t count = 0;
  for (const auto &kv : overloads_)
    count += kv.second.size();
  return count;
}

size_t GlobalIndex::guideCount() const {
  size_t count = 0;
  for (const auto &kv : guides_)
    count += kv.second.size();
  return count;
}

void GlobalIndex::addCoverageProperty(CoveragePropertyEntry entry) {
  coverageProps_[entry.enclosingClass].push_back(std::move(entry));
}

std::vector<const CoveragePropertyEntry *>
GlobalIndex::findClassMethods(const std::string &enclosingClass) const {
  std::vector<const CoveragePropertyEntry *> result;
  auto it = coverageProps_.find(enclosingClass);
  if (it != coverageProps_.end()) {
    for (const auto &entry : it->second)
      result.push_back(&entry);
  }
  return result;
}

std::vector<std::string> GlobalIndex::allIndexedClasses() const {
  std::vector<std::string> result;
  result.reserve(coverageProps_.size());
  for (const auto &kv : coverageProps_)
    result.push_back(kv.first);
  return result;
}

size_t GlobalIndex::coverageEntryCount() const {
  size_t count = 0;
  for (const auto &kv : coverageProps_)
    count += kv.second.size();
  return count;
}

} // namespace giga_drill

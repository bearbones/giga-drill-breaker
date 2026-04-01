#include "giga_drill/mugann/GlobalIndex.h"

namespace giga_drill {

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

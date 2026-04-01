#include "giga_drill/GlobalIndex.h"

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

} // namespace giga_drill

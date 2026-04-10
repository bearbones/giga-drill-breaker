#include "giga_drill/callgraph/ControlFlowIndex.h"

namespace giga_drill {

void ControlFlowIndex::addCallSiteContext(CallSiteContext ctx) {
  size_t idx = contexts_.size();
  byCallee_[ctx.calleeName].push_back(idx);
  byCaller_[ctx.callerName].push_back(idx);
  bySite_[ctx.callSite] = idx;
  contexts_.push_back(std::move(ctx));
}

const CallSiteContext *
ControlFlowIndex::contextAtSite(const std::string &callSite) const {
  auto it = bySite_.find(callSite);
  if (it == bySite_.end())
    return nullptr;
  return &contexts_[it->second];
}

std::vector<const CallSiteContext *>
ControlFlowIndex::contextsForCallee(const std::string &calleeName) const {
  std::vector<const CallSiteContext *> result;
  auto it = byCallee_.find(calleeName);
  if (it == byCallee_.end())
    return result;
  for (size_t idx : it->second)
    result.push_back(&contexts_[idx]);
  return result;
}

std::vector<const CallSiteContext *>
ControlFlowIndex::contextsForCaller(const std::string &callerName) const {
  std::vector<const CallSiteContext *> result;
  auto it = byCaller_.find(callerName);
  if (it == byCaller_.end())
    return result;
  for (size_t idx : it->second)
    result.push_back(&contexts_[idx]);
  return result;
}

std::vector<const CallSiteContext *>
ControlFlowIndex::protectedCallsTo(const std::string &calleeName) const {
  std::vector<const CallSiteContext *> result;
  auto it = byCallee_.find(calleeName);
  if (it == byCallee_.end())
    return result;
  for (size_t idx : it->second) {
    if (!contexts_[idx].enclosingTryCatches.empty())
      result.push_back(&contexts_[idx]);
  }
  return result;
}

std::vector<const CallSiteContext *>
ControlFlowIndex::unprotectedCallsTo(const std::string &calleeName) const {
  std::vector<const CallSiteContext *> result;
  auto it = byCallee_.find(calleeName);
  if (it == byCallee_.end())
    return result;
  for (size_t idx : it->second) {
    if (contexts_[idx].enclosingTryCatches.empty())
      result.push_back(&contexts_[idx]);
  }
  return result;
}

std::vector<const CallSiteContext *> ControlFlowIndex::allContexts() const {
  std::vector<const CallSiteContext *> result;
  result.reserve(contexts_.size());
  for (const auto &ctx : contexts_)
    result.push_back(&ctx);
  return result;
}

} // namespace giga_drill

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

#include "giga_drill/callgraph/ControlFlowIndex.h"

namespace giga_drill {

ControlFlowIndex::ControlFlowIndex(ControlFlowIndex &&other) noexcept
    : interner_(std::move(other.interner_)),
      contexts_(std::move(other.contexts_)),
      byCallee_(std::move(other.byCallee_)),
      byCaller_(std::move(other.byCaller_)),
      bySite_(std::move(other.bySite_)) {}

ControlFlowIndex &ControlFlowIndex::operator=(ControlFlowIndex &&other) noexcept {
  interner_ = std::move(other.interner_);
  contexts_ = std::move(other.contexts_);
  byCallee_ = std::move(other.byCallee_);
  byCaller_ = std::move(other.byCaller_);
  bySite_ = std::move(other.bySite_);
  return *this;
}

void ControlFlowIndex::addCallSiteContext(CallSiteContext ctx) {
  std::lock_guard<std::mutex> lock(mutex_);
  SId calleeId = interner_.intern(ctx.calleeName);
  SId callerId = interner_.intern(ctx.callerName);
  SId siteId = interner_.intern(ctx.callSite);
  size_t idx = contexts_.size();
  byCallee_[calleeId].push_back(idx);
  byCaller_[callerId].push_back(idx);
  bySite_[siteId] = idx;
  contexts_.push_back(std::move(ctx));
}

const CallSiteContext *
ControlFlowIndex::contextAtSite(const std::string &callSite) const {
  auto id = interner_.find(callSite);
  if (!id)
    return nullptr;
  auto it = bySite_.find(*id);
  if (it == bySite_.end())
    return nullptr;
  return &contexts_[it->second];
}

std::vector<const CallSiteContext *>
ControlFlowIndex::contextsForCallee(const std::string &calleeName) const {
  std::vector<const CallSiteContext *> result;
  auto id = interner_.find(calleeName);
  if (!id)
    return result;
  auto it = byCallee_.find(*id);
  if (it == byCallee_.end())
    return result;
  for (size_t idx : it->second)
    result.push_back(&contexts_[idx]);
  return result;
}

std::vector<const CallSiteContext *>
ControlFlowIndex::contextsForCaller(const std::string &callerName) const {
  std::vector<const CallSiteContext *> result;
  auto id = interner_.find(callerName);
  if (!id)
    return result;
  auto it = byCaller_.find(*id);
  if (it == byCaller_.end())
    return result;
  for (size_t idx : it->second)
    result.push_back(&contexts_[idx]);
  return result;
}

std::vector<const CallSiteContext *>
ControlFlowIndex::protectedCallsTo(const std::string &calleeName) const {
  std::vector<const CallSiteContext *> result;
  auto id = interner_.find(calleeName);
  if (!id)
    return result;
  auto it = byCallee_.find(*id);
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
  auto id = interner_.find(calleeName);
  if (!id)
    return result;
  auto it = byCallee_.find(*id);
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

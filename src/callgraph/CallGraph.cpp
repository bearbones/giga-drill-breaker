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

#include "giga_drill/callgraph/CallGraph.h"

#include <algorithm>

namespace giga_drill {

CallGraph::CallGraph(CallGraph &&other) noexcept
    : interner_(std::move(other.interner_)),
      nodes_(std::move(other.nodes_)),
      edges_(std::move(other.edges_)),
      outEdges_(std::move(other.outEdges_)),
      inEdges_(std::move(other.inEdges_)),
      derivedClasses_(std::move(other.derivedClasses_)),
      methodOverrides_(std::move(other.methodOverrides_)),
      effectiveImplClasses_(std::move(other.effectiveImplClasses_)),
      functionReturns_(std::move(other.functionReturns_)) {}

CallGraph &CallGraph::operator=(CallGraph &&other) noexcept {
  interner_ = std::move(other.interner_);
  nodes_ = std::move(other.nodes_);
  edges_ = std::move(other.edges_);
  outEdges_ = std::move(other.outEdges_);
  inEdges_ = std::move(other.inEdges_);
  derivedClasses_ = std::move(other.derivedClasses_);
  methodOverrides_ = std::move(other.methodOverrides_);
  effectiveImplClasses_ = std::move(other.effectiveImplClasses_);
  functionReturns_ = std::move(other.functionReturns_);
  return *this;
}

void CallGraph::addNode(CallGraphNode node) {
  std::lock_guard<std::mutex> lock(mutex_);
  SId nameId = interner_.intern(node.qualifiedName);
  auto it = nodes_.find(nameId);
  if (it == nodes_.end()) {
    nodes_.emplace(nameId, std::move(node));
  } else {
    if (!node.file.empty())
      it->second.file = std::move(node.file);
    if (node.line != 0)
      it->second.line = node.line;
    if (node.isEntryPoint)
      it->second.isEntryPoint = true;
    if (node.isVirtual)
      it->second.isVirtual = true;
    if (!node.enclosingClass.empty())
      it->second.enclosingClass = std::move(node.enclosingClass);
  }
}

void CallGraph::addEdge(CallGraphEdge edge) {
  std::lock_guard<std::mutex> lock(mutex_);
  SId callerId = interner_.intern(edge.callerName);
  SId calleeId = interner_.intern(edge.calleeName);
  size_t idx = edges_.size();
  outEdges_[callerId].push_back(idx);
  inEdges_[calleeId].push_back(idx);
  edges_.push_back(std::move(edge));
}

std::vector<const CallGraphEdge *>
CallGraph::calleesOf(const std::string &name) const {
  std::vector<const CallGraphEdge *> result;
  auto id = interner_.find(name);
  if (!id)
    return result;
  auto it = outEdges_.find(*id);
  if (it != outEdges_.end()) {
    for (size_t idx : it->second)
      result.push_back(&edges_[idx]);
  }
  return result;
}

std::vector<const CallGraphEdge *>
CallGraph::callersOf(const std::string &name) const {
  std::vector<const CallGraphEdge *> result;
  auto id = interner_.find(name);
  if (!id)
    return result;
  auto it = inEdges_.find(*id);
  if (it != inEdges_.end()) {
    for (size_t idx : it->second)
      result.push_back(&edges_[idx]);
  }
  return result;
}

size_t CallGraph::nodeCount() const { return nodes_.size(); }

size_t CallGraph::edgeCount() const { return edges_.size(); }

std::vector<const CallGraphNode *> CallGraph::allNodes() const {
  std::vector<const CallGraphNode *> result;
  result.reserve(nodes_.size());
  for (const auto &kv : nodes_)
    result.push_back(&kv.second);
  return result;
}

const CallGraphNode *
CallGraph::findNode(const std::string &qualifiedName) const {
  auto id = interner_.find(qualifiedName);
  if (!id)
    return nullptr;
  auto it = nodes_.find(*id);
  if (it != nodes_.end())
    return &it->second;
  return nullptr;
}

// --- Class hierarchy ---

void CallGraph::addDerivedClass(const std::string &baseClass,
                                const std::string &derivedClass) {
  std::lock_guard<std::mutex> lock(mutex_);
  SId baseId = interner_.intern(baseClass);
  SId derivedId = interner_.intern(derivedClass);
  auto &vec = derivedClasses_[baseId];
  if (std::find(vec.begin(), vec.end(), derivedId) == vec.end())
    vec.push_back(derivedId);
}

std::vector<std::string>
CallGraph::getDerivedClasses(const std::string &baseClass) const {
  auto id = interner_.find(baseClass);
  if (!id)
    return {};
  auto it = derivedClasses_.find(*id);
  if (it != derivedClasses_.end()) {
    std::vector<std::string> result;
    result.reserve(it->second.size());
    for (SId sid : it->second)
      result.push_back(interner_.resolve(sid));
    return result;
  }
  return {};
}

std::vector<std::string>
CallGraph::getAllDerivedClasses(const std::string &baseClass) const {
  auto baseId = interner_.find(baseClass);
  if (!baseId)
    return {};
  std::vector<std::string> result;
  std::vector<SId> stack = {*baseId};
  std::set<SId> visited;
  while (!stack.empty()) {
    SId cls = stack.back();
    stack.pop_back();
    if (!visited.insert(cls).second)
      continue;
    auto it = derivedClasses_.find(cls);
    if (it != derivedClasses_.end()) {
      for (SId derived : it->second) {
        result.push_back(interner_.resolve(derived));
        stack.push_back(derived);
      }
    }
  }
  return result;
}

// --- Virtual method overrides ---

void CallGraph::addMethodOverride(const std::string &baseMethod,
                                  const std::string &overrideMethod) {
  std::lock_guard<std::mutex> lock(mutex_);
  SId baseId = interner_.intern(baseMethod);
  SId overrideId = interner_.intern(overrideMethod);
  auto &vec = methodOverrides_[baseId];
  if (std::find(vec.begin(), vec.end(), overrideId) == vec.end())
    vec.push_back(overrideId);
}

std::vector<std::string>
CallGraph::getOverrides(const std::string &baseMethod) const {
  auto id = interner_.find(baseMethod);
  if (!id)
    return {};
  auto it = methodOverrides_.find(*id);
  if (it != methodOverrides_.end()) {
    std::vector<std::string> result;
    result.reserve(it->second.size());
    for (SId sid : it->second)
      result.push_back(interner_.resolve(sid));
    return result;
  }
  return {};
}

// --- Effective implementation mapping ---

void CallGraph::addEffectiveImpl(const std::string &concreteClass,
                                 const std::string &implMethod) {
  std::lock_guard<std::mutex> lock(mutex_);
  SId implId = interner_.intern(implMethod);
  SId classId = interner_.intern(concreteClass);
  effectiveImplClasses_[implId].insert(classId);
}

std::vector<std::string>
CallGraph::getClassesForImpl(const std::string &implMethod) const {
  auto id = interner_.find(implMethod);
  if (!id)
    return {};
  auto it = effectiveImplClasses_.find(*id);
  if (it != effectiveImplClasses_.end()) {
    std::vector<std::string> result;
    result.reserve(it->second.size());
    for (SId sid : it->second)
      result.push_back(interner_.resolve(sid));
    return result;
  }
  return {};
}

// --- Function returns ---

void CallGraph::addFunctionReturn(const std::string &funcName,
                                  const std::string &returnedFunc) {
  std::lock_guard<std::mutex> lock(mutex_);
  SId funcId = interner_.intern(funcName);
  SId retId = interner_.intern(returnedFunc);
  functionReturns_[funcId].insert(retId);
}

std::set<std::string>
CallGraph::getFunctionReturns(const std::string &funcName) const {
  auto id = interner_.find(funcName);
  if (!id)
    return {};
  auto it = functionReturns_.find(*id);
  if (it != functionReturns_.end()) {
    std::set<std::string> result;
    for (SId sid : it->second)
      result.insert(interner_.resolve(sid));
    return result;
  }
  return {};
}

} // namespace giga_drill

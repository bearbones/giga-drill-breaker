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
    : nodes_(std::move(other.nodes_)),
      edges_(std::move(other.edges_)),
      outEdges_(std::move(other.outEdges_)),
      inEdges_(std::move(other.inEdges_)),
      derivedClasses_(std::move(other.derivedClasses_)),
      methodOverrides_(std::move(other.methodOverrides_)),
      effectiveImplClasses_(std::move(other.effectiveImplClasses_)),
      functionReturns_(std::move(other.functionReturns_)) {}

CallGraph &CallGraph::operator=(CallGraph &&other) noexcept {
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
  auto name = node.qualifiedName;
  auto it = nodes_.find(name);
  if (it == nodes_.end()) {
    nodes_.emplace(std::move(name), std::move(node));
  } else {
    // Update with richer info if available.
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
  size_t idx = edges_.size();
  outEdges_[edge.callerName].push_back(idx);
  inEdges_[edge.calleeName].push_back(idx);
  edges_.push_back(std::move(edge));
}

std::vector<const CallGraphEdge *>
CallGraph::calleesOf(const std::string &name) const {
  std::vector<const CallGraphEdge *> result;
  auto it = outEdges_.find(name);
  if (it != outEdges_.end()) {
    for (size_t idx : it->second)
      result.push_back(&edges_[idx]);
  }
  return result;
}

std::vector<const CallGraphEdge *>
CallGraph::callersOf(const std::string &name) const {
  std::vector<const CallGraphEdge *> result;
  auto it = inEdges_.find(name);
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
  auto it = nodes_.find(qualifiedName);
  if (it != nodes_.end())
    return &it->second;
  return nullptr;
}

// --- Class hierarchy ---

void CallGraph::addDerivedClass(const std::string &baseClass,
                                const std::string &derivedClass) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto &vec = derivedClasses_[baseClass];
  if (std::find(vec.begin(), vec.end(), derivedClass) == vec.end())
    vec.push_back(derivedClass);
}

std::vector<std::string>
CallGraph::getDerivedClasses(const std::string &baseClass) const {
  auto it = derivedClasses_.find(baseClass);
  if (it != derivedClasses_.end())
    return it->second;
  return {};
}

std::vector<std::string>
CallGraph::getAllDerivedClasses(const std::string &baseClass) const {
  std::vector<std::string> result;
  std::vector<std::string> stack = {baseClass};
  std::set<std::string> visited;
  while (!stack.empty()) {
    std::string cls = std::move(stack.back());
    stack.pop_back();
    if (!visited.insert(cls).second)
      continue;
    auto it = derivedClasses_.find(cls);
    if (it != derivedClasses_.end()) {
      for (const auto &derived : it->second) {
        result.push_back(derived);
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
  auto &vec = methodOverrides_[baseMethod];
  if (std::find(vec.begin(), vec.end(), overrideMethod) == vec.end())
    vec.push_back(overrideMethod);
}

std::vector<std::string>
CallGraph::getOverrides(const std::string &baseMethod) const {
  auto it = methodOverrides_.find(baseMethod);
  if (it != methodOverrides_.end())
    return it->second;
  return {};
}

// --- Effective implementation mapping ---

void CallGraph::addEffectiveImpl(const std::string &concreteClass,
                                 const std::string &implMethod) {
  std::lock_guard<std::mutex> lock(mutex_);
  effectiveImplClasses_[implMethod].insert(concreteClass);
}

std::vector<std::string>
CallGraph::getClassesForImpl(const std::string &implMethod) const {
  auto it = effectiveImplClasses_.find(implMethod);
  if (it != effectiveImplClasses_.end())
    return {it->second.begin(), it->second.end()};
  return {};
}

// --- Function returns ---

void CallGraph::addFunctionReturn(const std::string &funcName,
                                  const std::string &returnedFunc) {
  std::lock_guard<std::mutex> lock(mutex_);
  functionReturns_[funcName].insert(returnedFunc);
}

std::set<std::string>
CallGraph::getFunctionReturns(const std::string &funcName) const {
  auto it = functionReturns_.find(funcName);
  if (it != functionReturns_.end())
    return it->second;
  return {};
}

} // namespace giga_drill

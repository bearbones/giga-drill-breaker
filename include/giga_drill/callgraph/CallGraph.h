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

#pragma once

#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace giga_drill {

enum class EdgeKind {
  DirectCall,
  VirtualDispatch,
  FunctionPointer,
  ConstructorCall,
  DestructorCall,
  OperatorCall,
  TemplateInstantiation
};

enum class Confidence {
  Proven,
  Plausible,
  Unknown
};

struct CallGraphNode {
  std::string qualifiedName;
  std::string file;
  unsigned line = 0;
  bool isEntryPoint = false;
  bool isVirtual = false;
  std::string enclosingClass;
};

struct CallGraphEdge {
  std::string callerName;
  std::string calleeName;
  EdgeKind kind;
  Confidence confidence;
  std::string callSite;
  unsigned indirectionDepth = 0;
};

class CallGraph {
public:
  void addNode(CallGraphNode node);
  void addEdge(CallGraphEdge edge);

  std::vector<const CallGraphEdge *> calleesOf(const std::string &name) const;
  std::vector<const CallGraphEdge *> callersOf(const std::string &name) const;

  size_t nodeCount() const;
  size_t edgeCount() const;

  std::vector<const CallGraphNode *> allNodes() const;
  const CallGraphNode *findNode(const std::string &qualifiedName) const;

  // Class hierarchy tracking.
  void addDerivedClass(const std::string &baseClass,
                       const std::string &derivedClass);
  std::vector<std::string>
  getDerivedClasses(const std::string &baseClass) const;
  std::vector<std::string>
  getAllDerivedClasses(const std::string &baseClass) const;

  // Virtual method override tracking.
  void addMethodOverride(const std::string &baseMethod,
                         const std::string &overrideMethod);
  std::vector<std::string>
  getOverrides(const std::string &baseMethod) const;

  // Effective implementation mapping: which concrete classes use a given
  // method implementation for virtual dispatch.
  void addEffectiveImpl(const std::string &concreteClass,
                        const std::string &implMethod);
  std::vector<std::string>
  getClassesForImpl(const std::string &implMethod) const;

  // Function pointer return value tracking.
  void addFunctionReturn(const std::string &funcName,
                         const std::string &returnedFunc);
  std::set<std::string>
  getFunctionReturns(const std::string &funcName) const;

private:
  std::unordered_map<std::string, CallGraphNode> nodes_;
  std::vector<CallGraphEdge> edges_;
  std::unordered_map<std::string, std::vector<size_t>> outEdges_;
  std::unordered_map<std::string, std::vector<size_t>> inEdges_;

  std::unordered_map<std::string, std::vector<std::string>> derivedClasses_;
  std::unordered_map<std::string, std::vector<std::string>> methodOverrides_;
  std::unordered_map<std::string, std::set<std::string>> effectiveImplClasses_;
  std::unordered_map<std::string, std::set<std::string>> functionReturns_;
};

} // namespace giga_drill

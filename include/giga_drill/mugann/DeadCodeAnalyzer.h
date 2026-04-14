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

#include "giga_drill/callgraph/CallGraph.h"
#include "giga_drill/mugann/GlobalIndex.h"

#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace giga_drill {

enum class Liveness {
  Alive,              // Reachable via proven paths.
  OptimisticallyAlive, // Reachable only via plausible paths.
  Dead                // Not reachable.
};

class DeadCodeAnalyzer {
public:
  DeadCodeAnalyzer(const CallGraph &graph,
                   const std::vector<std::string> &entryPoints,
                   const std::vector<std::string> &publicApi = {});

  // Run pessimistic analysis (Proven edges only).
  void analyzePessimistic();

  // Run optimistic analysis (Proven + Plausible edges).
  void analyzeOptimistic();

  // Get combined results: Alive if pessimistic says alive,
  // OptimisticallyAlive if only optimistic says alive, Dead otherwise.
  std::unordered_map<std::string, Liveness> getResults() const;

  // Emit Diagnostic entries for dead code.
  std::vector<Diagnostic> getDiagnostics() const;

private:
  const CallGraph &graph_;
  std::vector<std::string> entryPoints_;
  std::set<std::string> publicApi_;

  std::set<std::string> pessimisticAlive_;
  std::set<std::string> optimisticAlive_;

  void bfs(bool includeVirtualPlausible, std::set<std::string> &alive);
};

} // namespace giga_drill

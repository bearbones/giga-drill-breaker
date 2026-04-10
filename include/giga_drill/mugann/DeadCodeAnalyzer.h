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

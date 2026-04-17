// Copyright (c) 2026 The giga-drill-breaker Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0

// main.cpp — entry point for the deep_chains call-graph fixture.
//
// Two chains, both >=6 layers, with a mix of Proven and Plausible edges
// at every layer:
//
//   Chain A: main -> Pipeline::run -> stage1_ingest -> stage2_parse
//                 -> stage3_transform -> stage4_dispatch -> stage5_sink
//
//   Chain B: main -> Pipeline::runAsync -> Scheduler::schedule
//                 -> Worker::execute -> NetworkWorker::execute
//                 -> tcpWriteBytes
//
// Every layer emits at least one Proven out-edge (DirectCall, typically)
// and at least one Plausible out-edge (FunctionPointer via address-take
// or VirtualDispatch via base ref). See expected_chains.json for details.

#include "callbacks.hpp"
#include "pipeline.hpp"

int main() {
  // Plausible: address-take a free function and stash it in a local,
  // so main itself emits a Plausible out-edge alongside its Proven
  // DirectCall edges to Pipeline methods.
  CallbackFn boot = &cbs::startupHook;
  (void)boot;

  Pipeline p;

  // Proven: direct call into Chain A.
  int a = p.run(7);

  // Proven: direct call into Chain B.
  int b = p.runAsync(1);

  return (a + b) & 1;
}

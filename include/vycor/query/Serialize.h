// Copyright (c) 2026 The vycor-cpp Authors
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

#include "vycor/callgraph/CallGraph.h"
#include "vycor/callgraph/ChannelIndex.h"
#include "vycor/callgraph/ConditionalGuard.h"

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/JSON.h"

#include <optional>

// Enum <-> string spellings and JSON serializers shared by the query tools.
// These spellings are part of the tool output contract.

namespace vycor {

const char *edgeKindToString(EdgeKind k);
EdgeKind parseEdgeKind(llvm::StringRef s);

const char *executionContextToString(ExecutionContext c);
std::optional<ExecutionContext> parseExecutionContext(llvm::StringRef s);

const char *confidenceToString(Confidence c);
Confidence parseConfidence(llvm::StringRef s);
int confidenceRank(Confidence c);

llvm::json::Value edgeToJson(const CallGraphEdge &e);

const char *channelOperationToString(ChannelOperation op);
llvm::json::Value serializeGuard(const ConditionalGuard &g);
llvm::json::Value serializeChannelSite(const ChannelSite &s);

} // namespace vycor

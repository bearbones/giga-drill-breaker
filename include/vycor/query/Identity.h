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

#include "vycor/query/Tools.h"

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/JSON.h"

#include <optional>
#include <string>
#include <vector>

// F8 identity resolution (docs/design-f8-usr-identity.md §4): how a tool
// turns its name/usr/site/filter parameters into one USR, and the
// disambiguation payload it returns when it cannot.

namespace vycor {

/// Candidate-list cap for disambiguation responses (see Identity.cpp).
constexpr size_t kMaxAmbiguousCandidates = 25;

/// Non-error disambiguation response for an ambiguous display name: the
/// client picks a candidate and re-queries with its `usr`, or refines with
/// the `site`/`filter` parameters named in the note. Candidates are sorted
/// by usr string so the response is deterministic.
llvm::json::Value makeAmbiguousNameResult(const ToolContext &ctx,
                                          llvm::StringRef paramName,
                                          llvm::StringRef name,
                                          std::vector<std::string> usrs,
                                          llvm::StringRef filterNote = "");

/// Companion parameter name for an identity's usr parameter: "usr" ->
/// "site"/"filter", "to_usr" -> "to_site"/"to_filter", "fn_a_usr" ->
/// "fn_a_site"/"fn_a_filter".
std::string companionParam(llvm::StringRef usrParam, llvm::StringRef suffix);

/// Resolve an identity parameter per the disambiguation contract (see the
/// comment on the definition). Returns the USR, or nullopt with
/// `ambiguous` set to the payload the handler must return; nullopt with
/// `ambiguous` unset means no identity parameter was present.
std::optional<std::string>
resolveIdentity(const llvm::json::Object &args, const ToolContext &ctx,
                llvm::StringRef nameParam, llvm::StringRef usrParam,
                std::optional<llvm::json::Value> &ambiguous);

/// Adds the resolved node usr to a response object when the identity names
/// a registered node (unregistered endpoints have no node to cite).
void attachUsr(llvm::json::Object &obj, const ToolContext &ctx,
               const std::string &ident, llvm::StringRef key = "usr");

} // namespace vycor

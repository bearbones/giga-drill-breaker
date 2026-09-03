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

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/JSON.h"

namespace vycor {

// ============================================================================
// JSON Schema builders for tool input schemas
// ============================================================================

inline llvm::json::Value stringProp(llvm::StringRef desc) {
  llvm::json::Object p;
  p["type"] = "string";
  p["description"] = desc.str();
  return llvm::json::Value(std::move(p));
}

inline llvm::json::Value intProp(llvm::StringRef desc) {
  llvm::json::Object p;
  p["type"] = "integer";
  p["description"] = desc.str();
  return llvm::json::Value(std::move(p));
}

inline llvm::json::Value boolProp(llvm::StringRef desc) {
  llvm::json::Object p;
  p["type"] = "boolean";
  p["description"] = desc.str();
  return llvm::json::Value(std::move(p));
}

inline llvm::json::Value stringArrayProp(llvm::StringRef desc) {
  llvm::json::Object items;
  items["type"] = "string";
  llvm::json::Object p;
  p["type"] = "array";
  p["items"] = std::move(items);
  p["description"] = desc.str();
  return llvm::json::Value(std::move(p));
}

// The two identity-refinement parameters every identity-taking tool accepts
// alongside <prefix>usr (resolved in resolveIdentity; see the disambiguation
// contract there). `prefix` is "" for tools with a single identity, or
// "to_"/"from_"/"fn_a_"/"fn_b_" for multi-identity tools.
inline void addIdentityRefinementProps(llvm::json::Object &props,
                                       llvm::StringRef prefix) {
  props[(prefix + "site").str()] = stringProp(
      "Call site 'file:line:col' (as spelled in the compile command). "
      "Resolves the identity to the exact overload/template instantiation "
      "called at that site — the most precise disambiguator when you are "
      "looking at a specific call.");
  props[(prefix + "filter").str()] = stringProp(
      "Literal substring matched against candidate usr, qualified name, and "
      "file to narrow an ambiguous name; resolves when exactly one "
      "candidate survives. NOT type-aware: use names that appear literally "
      "(e.g. a template argument's class name), not guessed signatures.");
}


} // namespace vycor

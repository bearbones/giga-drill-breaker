// Copyright 2025 Alex Mason
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "vycor/cli/BakeConfig.h"

#include "vycor/ext/Extensions.h"

#include "llvm/Support/JSON.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>

namespace vycor {

// ---------------------------------------------------------------------------
// --channel-types-json parsing
// ---------------------------------------------------------------------------

// Parses a JSON array of channel type registrations:
//   [{"type": "Queue", "produce": ["push"], "consume": ["pop"],
//     "category": "queue"}, ...]
// "type" must be the canonical type name WITHOUT the struct/class keyword
// (see ChannelIndex.h). Returns false (and prints a diagnostic) on any
// malformed entry; the caller should treat that as a fatal CLI error, same
// as a bad --rules-json would be for morph.
bool parseChannelTypesJson(const std::string &path,
                                  vycor::ChannelTypeConfig &outCfg) {
  auto bufOrErr = llvm::MemoryBuffer::getFile(path);
  if (!bufOrErr) {
    llvm::errs() << "channel-types-json: cannot read " << path << ": "
                 << bufOrErr.getError().message() << "\n";
    return false;
  }
  auto jsonOrErr = llvm::json::parse(bufOrErr.get()->getBuffer());
  if (!jsonOrErr) {
    llvm::errs() << "channel-types-json: parse error in " << path << ": "
                 << llvm::toString(jsonOrErr.takeError()) << "\n";
    return false;
  }
  auto *arr = jsonOrErr->getAsArray();
  if (!arr) {
    llvm::errs() << "channel-types-json: " << path
                 << " must contain a top-level JSON array\n";
    return false;
  }
  for (const auto &entry : *arr) {
    auto *obj = entry.getAsObject();
    if (!obj) {
      llvm::errs() << "channel-types-json: each entry must be an object\n";
      return false;
    }
    vycor::ChannelTypeSpec spec;
    if (auto type = obj->getString("type")) {
      spec.qualifiedTypeName = type->str();
    } else {
      llvm::errs() << "channel-types-json: entry missing required 'type'\n";
      return false;
    }
    if (auto *produce = obj->getArray("produce"))
      for (const auto &m : *produce)
        if (auto s = m.getAsString())
          spec.produceMethods.push_back(s->str());
    if (auto *consume = obj->getArray("consume"))
      for (const auto &m : *consume)
        if (auto s = m.getAsString())
          spec.consumeMethods.push_back(s->str());
    if (auto category = obj->getString("category"))
      spec.category = category->str();
    outCfg.registeredTypes.push_back(std::move(spec));
  }
  return true;
}

// ---------------------------------------------------------------------------
// --org-config loading and merging (see docs/EXTENDING.md)
// ---------------------------------------------------------------------------

// Loads --org-config when set and installs its hook-shaped parts (feature
// flag patterns, lock/channel types) into ExtensionRegistry. Compiled ext/
// registrars have already run by this point (static init), so after this
// call the registry holds both sources. Returns false (with a diagnostic)
// on unreadable/malformed config.
bool loadOrgConfigIfSet(const std::string &path,
                               vycor::OrgConfig &out) {
  if (path.empty())
    return true;
  std::string err;
  if (!vycor::loadOrgConfigFile(path, out, err) ||
      !vycor::applyOrgConfig(out, err)) {
    llvm::errs() << "org-config: " << err << "\n";
    return false;
  }
  return true;
}

// Merges registry-held lock/channel types (compiled ext/ registrars plus
// --org-config) into the CLI-built configs, and org collapse paths into
// collapsePaths. CLI entries keep their position and duplicates are
// dropped: the merged lists land in snapshot meta (config-match check), so
// the result must be deterministic and must equal the plain CLI lists when
// no extensions are registered.
void mergeExtensionConfig(const vycor::OrgConfig &orgCfg,
                                 vycor::LockTypeConfig &lockCfg,
                                 vycor::ChannelTypeConfig &channelCfg,
                                 std::vector<std::string> &collapsePaths) {
  const auto &registry = vycor::ExtensionRegistry::instance();
  for (const auto &name : registry.lockTypes())
    if (std::find(lockCfg.userAllowlist.begin(), lockCfg.userAllowlist.end(),
                  name) == lockCfg.userAllowlist.end())
      lockCfg.userAllowlist.push_back(name);
  for (const auto &spec : registry.channelTypes())
    if (std::find(channelCfg.registeredTypes.begin(),
                  channelCfg.registeredTypes.end(),
                  spec) == channelCfg.registeredTypes.end())
      channelCfg.registeredTypes.push_back(spec);
  for (const auto &pattern : orgCfg.collapsePaths)
    if (std::find(collapsePaths.begin(), collapsePaths.end(), pattern) ==
        collapsePaths.end())
      collapsePaths.push_back(pattern);
}

} // namespace vycor

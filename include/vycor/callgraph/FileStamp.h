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

#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace vycor {

/// A file's identity for warm-start dirty checks: mtime + size. TU stamps
/// come from SnapshotIO::stampFiles (nanosecond mtime, taken before the
/// parse); dependency stamps come from the frontend's own stat of each
/// file it opened (whole seconds, see SnapshotIO::dirtyTUs).
struct FileStamp {
  std::string path;
  uint64_t mtimeNs = 0; // last modification, nanoseconds since epoch
  uint64_t size = 0;

  bool operator==(const FileStamp &o) const {
    return path == o.path && mtimeNs == o.mtimeNs && size == o.size;
  }
};

/// Per TU (absolute, dot-free path), every file the frontend opened while
/// parsing it — headers, .inc/.def, the PCH's inputs — with the stamp the
/// frontend saw, the TU itself excluded. Produced by the bake, recorded in
/// the snapshot meta so a header edit dirties its includers.
using TuDependencies =
    std::unordered_map<std::string, std::vector<FileStamp>>;

} // namespace vycor

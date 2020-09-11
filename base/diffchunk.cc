//
// Copyright 2007 Google LLC
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
//

#include "base/diffchunk.h"

#include "base/logging.h"
#include "absl/strings/string_view.h"

namespace file_based_test_driver_base {
namespace {

constexpr absl::string_view kChunkOpCodesStrings[] = {
    "equal", "insert", "delete", "replace", "???"};
}  // namespace

absl::string_view DiffChunk::opcode() const { return DiffChunk::opcode(type); }

absl::string_view DiffChunk::opcode(ChunkType type) {
  if (type > kMaxChunkType) {
    LOG(WARNING) << "Invalid chunk type. Ignoring.";
    return kChunkOpCodesStrings[kMaxChunkType];
  }
  return kChunkOpCodesStrings[type];
}
}  // namespace file_based_test_driver_base

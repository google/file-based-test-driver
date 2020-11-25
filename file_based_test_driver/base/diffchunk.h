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

#ifndef THIRD_PARTY_FILE_BASED_TEST_DRIVER_BASE_DIFFCHUNK_H__
#define THIRD_PARTY_FILE_BASED_TEST_DRIVER_BASE_DIFFCHUNK_H__

#include <string>

#include "absl/strings/string_view.h"

namespace file_based_test_driver_base {

// The output of ReDiff is a sequence of DiffChunks that represent edit
// operations to be performed to go from the source text A to the destination
// text B. Each chunk is described by an opcode, a "source region"
// [source_first, source_last) and a "target region" [first_line, last_line),
// which are described by 0-based line indices in documents A and B
// respectively. A value of zero for source_last or last_line indicates a 1 line
// long region at source_first or first_line.
//
// The following four opcodes are identical to those produced by the Python
// library difflib.SequenceMatcher
// (http://docs.python.org/lib/sequence-matcher.html):
//
// UNCHANGED:          The source region == the target region. It is guaranteed
//                     that the regions have equal length.
// ADDED:              Insert a copy of the target region at position
//                     source_first in A. source_last is ignored.
// REMOVED:            Delete the source region. Target region is ignored.
// CHANGED:            Replace the source region with the target region. This
//                     is equivalent to a REMOVED followed by an ADDED.
//
//
// The IGNORED opcode should never be produced in output; it's used internally
// by ReDiff.

// This list of chunk types needs to be kept in sync with
// DiffChunk::ChunkOpCodes.
enum ChunkType { UNCHANGED, ADDED, REMOVED, CHANGED, IGNORED };

// Encapsulates information about a diff chunk.
struct DiffChunk {
  static constexpr ChunkType kMaxChunkType = IGNORED;
  DiffChunk() {}

  // source_first/last represent the range of lines within the source file
  // that this chunk corresponds to (left-hand side).
  int source_first = 0;
  int source_last = 0;

  // first/last_line represent the range of lines within the destination file
  // that this chunk corresponds to (right-hand side).
  int first_line = 0;
  int last_line = 0;

  ChunkType type = UNCHANGED;

  absl::string_view opcode() const;
  static absl::string_view opcode(ChunkType type);
};

}  // namespace file_based_test_driver_base

#endif  // THIRD_PARTY_FILE_BASED_TEST_DRIVER_BASE_DIFFCHUNK_H__

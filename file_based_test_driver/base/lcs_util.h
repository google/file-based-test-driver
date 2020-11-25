//
// Copyright 2012 Google LLC
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

// Helper functions for collecting chunks.
#ifndef THIRD_PARTY_FILE_BASED_TEST_DRIVER_BASE_LCS_UTIL_H_
#define THIRD_PARTY_FILE_BASED_TEST_DRIVER_BASE_LCS_UTIL_H_

#include <vector>

#include "file_based_test_driver/base/lcs.h"

namespace file_based_test_driver_base {

// Returns true if the after chunk starts immediately after the before chunk.
// Returns false in all other cases, including overlapping cases or cases
// where the chunks are presented in the wrong order.
bool CanBeMerged(const Chunk& before, const Chunk& after);

// Appends a chunk at the end of a chunk vector. If possible, the new chunk is
// merged with the last chunk of the vector. Chunks are assumed to be appended
// in ascending order. Empty chunks are ignored.
void AppendChunk(int left, int right, int len, std::vector<Chunk>* chunks);

// Same as AppendChunk but assuming that chunks are appended in reverse order.
void AppendReverseChunk(int left, int right, int len,
                        std::vector<Chunk>* chunks);

// Chunks which have been appended via AppendReverseChunk can be reordered
// with this function. The reordering starts at index first_chunk. If possible,
// the reordered chunks are merged with the preceding sequence.
void ReorderReverseChunks(int first_chunk, std::vector<Chunk>* chunks);

}  // namespace file_based_test_driver_base

#endif  // THIRD_PARTY_FILE_BASED_TEST_DRIVER_BASE_LCS_UTIL_H_

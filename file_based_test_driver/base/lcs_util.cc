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

#include "file_based_test_driver/base/lcs_util.h"

#include <algorithm>
#include <iterator>
#include <vector>

#include <cstdint>
#include "file_based_test_driver/base/lcs.h"

namespace file_based_test_driver_base {

static const float kHuntFactor     = 0.000000037547156f;
static const float kMyersFactor    = 0.000000001179166f;
static const float kInitFactor     = 0.000000002785935f;
static const float kEstimateFactor = 0.000000003818995f;

LcsOptions::LcsOptions()
    : hunt_factor_(kHuntFactor),
      myers_factor_(kMyersFactor),
      init_factor_(kInitFactor),
      estimate_factor_(kEstimateFactor),
      lcs_bound_ratio_(0.7),
      max_memory_(1 << 20),  // 1 MB should be sufficient for most cases.
      max_keys_(std::numeric_limits<int32_t>::max()) {}

bool CanBeMerged(const Chunk& before, const Chunk& after) {
  return before.left + before.length == after.left &&
      before.right + before.length == after.right;
}

void AppendChunk(int left, int right, int len, std::vector<Chunk>* chunks) {
  if (len == 0)
    return;
  Chunk new_chunk(left, right, len);
  if (!chunks->empty() && CanBeMerged(chunks->back(), new_chunk))
    chunks->back().length += len;
  else
    chunks->push_back(new_chunk);
}

void AppendReverseChunk(int left, int right, int len,
                        std::vector<Chunk>* chunks) {
  if (len == 0)
    return;
  Chunk new_chunk(left, right, len);
  if (!chunks->empty() && CanBeMerged(new_chunk, chunks->back())) {
    chunks->back().left -= len;
    chunks->back().right -= len;
    chunks->back().length += len;
  } else {
    chunks->push_back(new_chunk);
  }
}

void ReorderReverseChunks(int first_chunk, std::vector<Chunk>* chunks) {
  if (first_chunk > 0 &&
      CanBeMerged((*chunks)[first_chunk - 1], chunks->back())) {
    (*chunks)[first_chunk - 1].length += chunks->back().length;
    chunks->pop_back();
  }
  std::reverse(chunks->begin() + first_chunk, chunks->end());
}

}  // namespace file_based_test_driver_base

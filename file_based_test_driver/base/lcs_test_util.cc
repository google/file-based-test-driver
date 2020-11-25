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

#include "file_based_test_driver/base/lcs_test_util.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "gtest/gtest.h"
#include "absl/random/bit_gen_ref.h"
#include "absl/random/distributions.h"
#include "absl/strings/string_view.h"
#include "file_based_test_driver/base/lcs.h"

namespace file_based_test_driver_base {
namespace internal {

bool Equals(const Chunk& chunk, int left, int right, int len) {
  return chunk.left == left && chunk.right == right && chunk.length == len;
}

int RunSimpleLcs(const char* left, int left_size,
                 const char* right, int right_size) {
  std::vector<int> prev_col(right_size + 1, 0);
  std::vector<int> curr_col(right_size + 1, 0);
  for (int x = 1; x <= left_size; x++) {
    for (int y = 1; y <= right_size; y++) {
      if (left[x - 1] == right[y - 1])
        curr_col[y] = prev_col[y - 1] + 1;
      else
        curr_col[y] = std::max(prev_col[y], curr_col[y - 1]);
    }
    using std::swap;
    swap(prev_col, curr_col);
  }
  return prev_col.back();
}

std::string RandomString(absl::BitGenRef rand, int n, char min_char,
                         char max_char) {
  std::string output;
  while (n-- > 0)
    output += static_cast<char>(absl::Uniform<uint8_t>(
        rand, static_cast<uint8_t>(min_char), static_cast<uint8_t>(max_char) + 1));
  return output;
}

void VerifyChunks(const char* left, int left_size,
                  const char* right, int right_size,
                  const std::vector<Chunk>& chunks, int expected_lcs) {
  // Verify order of chunks.
  for (int i = 1; i < chunks.size(); i++) {
    const Chunk& previous(chunks[i - 1]);
    const Chunk& current(chunks[i]);
    EXPECT_LE(previous.left + previous.length, current.left)
        << "Overlapping chunk for the left side!";
    EXPECT_LE(previous.right + previous.length, current.right)
        << "Overlapping chunk for the right side!\n"
        << left << "\n" << right;
    EXPECT_FALSE(previous.left + previous.length == current.left &&
                 previous.right + previous.length == current.right)
        << "Chunks have not been merged!";
  }

  // Verify chunk content
  for (int i = 0; i < chunks.size(); i++) {
    const Chunk& current(chunks[i]);
    EXPECT_LT(0, current.length)
        << "Chunks with zero length are not allowed!";
    EXPECT_EQ(absl::string_view(left + current.left, current.length),
              absl::string_view(right + current.right, current.length))
        << "Chunk has different content on left and right side!\n"
        << left << "\n"
        << right;
  }

  // Compute length of longest common subsequence from chunks
  int lcs = 0;
  for (int i = 0; i < chunks.size(); i++)
    lcs += chunks[i].length;
  EXPECT_EQ(expected_lcs, lcs);
}

}  // namespace internal
}  // namespace file_based_test_driver_base

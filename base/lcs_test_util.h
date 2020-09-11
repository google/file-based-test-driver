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

// Some generic test functions for testing LCS algorithms.
#ifndef THIRD_PARTY_FILE_BASED_TEST_DRIVER_BASE_LCS_TEST_UTIL_H_
#define THIRD_PARTY_FILE_BASED_TEST_DRIVER_BASE_LCS_TEST_UTIL_H_

#include <string>
#include <vector>

#include "absl/random/bit_gen_ref.h"
#include "base/lcs.h"
#include "base/lcs_util.h"

class ACMRandom;

namespace file_based_test_driver_base {
namespace internal {

// Checks whether a Chunk is set to the specified parameters.
bool Equals(const Chunk& chunk, int left, int right, int len);

// Computes LCS with the straight-forward dynamic programming scheme. It
// consumes O(right_size) memory and runs in O(left_size * right_size) time.
int RunSimpleLcs(const char* left, int left_size,
                 const char* right, int right_size);

// Returns a random input string of length n whose characters are in the range
// [min_char, max_char].
std::string RandomString(absl::BitGenRef rand, int n, char min_char,
                         char max_char);

// Verifies whether the passed chunks are sorted, reference the valid substrings
// which are identical in both sequences, and describe a subsequence of the
// expected length.
void VerifyChunks(const char* left, int left_size,
                  const char* right, int right_size,
                  const std::vector<Chunk>& chunks, int expected_lcs);

}  // namespace internal
}  // namespace file_based_test_driver_base

#endif  // THIRD_PARTY_FILE_BASED_TEST_DRIVER_BASE_LCS_TEST_UTIL_H_

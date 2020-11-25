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

#include "file_based_test_driver/base/lcs_hybrid.h"

#include <string.h>

#include <algorithm>
#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "absl/random/random.h"
#include "file_based_test_driver/base/lcs.h"
#include "file_based_test_driver/base/lcs_hunt.h"
#include "file_based_test_driver/base/lcs_test_util.h"

namespace file_based_test_driver_base {
namespace internal {

// We run here different versions against each other, i.e.
//   with/without chunks, Myers, Hunt, recursive
// and verify that they agree with each other.
int RunHybrid(const char* left, const char* right, std::vector<Chunk>* chunks) {
  int left_size = strlen(left);
  int right_size = strlen(right);
  LcsOptions options;
  options.set_max_keys(256);

  LcsHybrid<char> hybrid;
  hybrid.set_options(options);
  int lcs = hybrid.Run(left, left_size, 0, right, right_size, 0, chunks);

  // Compare with run without chunks.
  int lcs_no_chunks =
      hybrid.Run(left, left_size, 0, right, right_size, 0, nullptr);
  EXPECT_EQ(lcs, lcs_no_chunks);

  // Enforce Myers' algorithm.
  options.set_myers_factor(0.f);
  hybrid.set_options(options);
  int lcs_myers = hybrid.Run(left, left_size, 0, right, right_size, 0, nullptr);
  EXPECT_EQ(lcs, lcs_myers);

  // Enforce Hunt's algorithm.
  options.set_myers_factor(1.f);
  options.set_hunt_factor(0.f);
  options.set_init_factor(0.f);
  options.set_estimate_factor(0.f);
  hybrid.set_options(options);
  int lcs_hunt = hybrid.Run(left, left_size, 0, right, right_size, 0, nullptr);
  EXPECT_EQ(lcs, lcs_hunt);

  // Enforce recursive implementation.
  KeyOccurrences right_occ;
  right_occ.Init(right, right_size, options.max_keys());
  LcsStats stats(left, left_size, right_occ);
  LcsHybridEstimator estimator(left_size, right_size, &options);
  estimator.set_stats(&stats);
  options.set_max_memory(
      std::max(estimator.InitMemory(), estimator.HuntsSplitMemory()));
  int lcs_recursive =
      hybrid.Run(left, left_size, 0, right, right_size, 0, nullptr);
  EXPECT_EQ(lcs, lcs_recursive);

  return lcs;
}

TEST(LcsHybridEstimator, MemoryAndRuntime) {
  const char kLeft[] = "ababbaaabbabaabaaaaababba";  // 15 a's, 10 b's
  const char kRight[] = "aaabbaaabab";  // 7 a's, 4 b's

  LcsOptions options;
  options.set_max_keys(256);
  options.set_lcs_bound_ratio(0.0f);
  LcsHybridEstimator estimator(strlen(kLeft), strlen(kRight), &options);
  EXPECT_EQ(1520, estimator.MyersWorstCaseMemory());  // (18+2) * (18+1) * 4
  EXPECT_EQ(152, estimator.MyersSplitMemory());       // (25 + 11 + 2) * 4
  EXPECT_EQ(1072, estimator.KeyOccurrencesMemory());  // (257 + 11) * 4
  EXPECT_EQ(2100, estimator.InitMemory());  // 257 * 4 + 1072
  EXPECT_EQ(1248, estimator.HuntsSplitMemory());  // 11 * 16 + 1072

  // Init stats
  KeyOccurrences right_occ;
  right_occ.Init(kRight, strlen(kRight), options.max_keys());
  LcsStats stats(kLeft, strlen(kLeft), right_occ);
  estimator.set_stats(&stats);

  int lower_bound, upper_bound;
  stats.DiffBounds(&lower_bound, &upper_bound);
  EXPECT_EQ(14, lower_bound);  // left has 14 chars more than right.
  EXPECT_EQ(28, upper_bound);

  EXPECT_EQ(145, stats.beta());  // 15 * 7 + 4 * 10

  EXPECT_EQ(960, estimator.MyersMemory());   // 16 * 15 * 4
  EXPECT_EQ(2856, estimator.HuntsMemory());  // 145 * 12 + 11 * 4 + 1072

  options.set_init_factor(0.0f);
  options.set_estimate_factor(0.0f);
  options.set_hunt_factor(1.0f);
  options.set_myers_factor(1.0f);
  options.set_max_keys(2);

  EXPECT_FLOAT_EQ(784, estimator.MyersRuntime());  // 28^2
  EXPECT_FLOAT_EQ(145, estimator.HuntsRuntime());  // 7 * 15 + 4 * 10
  EXPECT_FLOAT_EQ(1296, estimator.MyersWorstCaseRuntime());  // (25 + 11)^2
  EXPECT_FLOAT_EQ(137.5, estimator.HuntsBestCaseRuntime());  // 11 / 2 * 25
}

TEST(LcsHybridEstimator, MemoryExceeded) {
  const char kLeft[] = "ababbaaabbabaabaaaaababba";  // 15 a's, 10 b's
  const char kRight[] = "baaabaaabab";  // 7 a's, 4 b's
  int left_size = strlen(kLeft);
  int right_size = strlen(kRight);

  LcsOptions options;
  options.set_max_keys(256);

  LcsHybridEstimator estimator(left_size, right_size, &options);
  KeyOccurrences right_occ;
  right_occ.Init(kRight, right_size, options.max_keys());
  LcsStats stats(kLeft, left_size, right_occ);
  estimator.set_stats(&stats);

  // Give the algorithm a byte less memory than required.
  options.set_max_memory(estimator.MyersSplitMemory() - 1);
  LcsHybrid<char> hybrid;
  hybrid.set_options(options);
  int lcs = hybrid.Run(kLeft, left_size, 0, kRight, right_size, 0, nullptr);
  EXPECT_EQ(kLcsMemoryLimitExceeded, lcs);

  // Give the algorithm the minimum possible memory required.
  options.set_max_memory(estimator.MyersSplitMemory());
  hybrid.set_options(options);
  lcs = hybrid.Run(kLeft, left_size, 0, kRight, right_size, 0, nullptr);
  EXPECT_EQ(11, lcs);
}

TEST(LcsHybrid, Equal) {
  const char kLeft[] = "ababbaa";
  const char kRight[] = "ababbaa";
  std::vector<Chunk> chunks;
  int lcs = RunHybrid(kLeft, kRight, &chunks);
  EXPECT_EQ(7, lcs);
  EXPECT_EQ(1, chunks.size());
  EXPECT_TRUE(Equals(chunks[0], 0, 0, strlen(kLeft)));
}

TEST(LcsHybrid, DeletionOnRightSide) {
  const char kLeft[] = "ababbaa";
  const char kRight[] = "ababaa";
  std::vector<Chunk> chunks;
  int lcs = RunHybrid(kLeft, kRight, &chunks);
  EXPECT_EQ(6, lcs);
  EXPECT_EQ(2, chunks.size());
  EXPECT_TRUE(Equals(chunks[0], 0, 0, 4));
  EXPECT_TRUE(Equals(chunks[1], 5, 4, 2));
}

TEST(LcsHybrid, DeletionOnLeftSide) {
  const char kLeft[] = "abbbaa";
  const char kRight[] = "ababbaa";
  std::vector<Chunk> chunks;
  int lcs = RunHybrid(kLeft, kRight, &chunks);
  EXPECT_EQ(6, lcs);
  EXPECT_EQ(2, chunks.size());
  EXPECT_TRUE(Equals(chunks[0], 0, 0, 2));
  EXPECT_TRUE(Equals(chunks[1], 2, 3, 4));
}

TEST(LcsHybrid, EmptySequence) {
  const char kLeft[] = "ababbaa";
  const char kRight[] = "";
  std::vector<Chunk> chunks;
  int lcs = RunHybrid(kLeft, kRight, &chunks);
  EXPECT_EQ(0, lcs);
  EXPECT_EQ(0, chunks.size());

  lcs = RunHybrid(kRight, kLeft, &chunks);
  EXPECT_EQ(0, lcs);
  EXPECT_EQ(0, chunks.size());
}

TEST(LcsHybrid, RandomSequence) {
  absl::BitGen gen;
  for (int k = 0; k < 100; k++) {
    std::string left =
        RandomString(gen, absl::Uniform<int>(gen, 0, 100), 'a', 'b');
    std::string right =
        RandomString(gen, absl::Uniform<int>(gen, 0, 100), 'a', 'b');
    std::vector<Chunk> chunks;
    int lcs  = RunSimpleLcs(left.c_str(), left.size(),
                            right.c_str(), right.size());
    int lcs2 = RunHybrid(left.c_str(), right.c_str(), &chunks);
    EXPECT_EQ(lcs, lcs2) << left << " " << right;
    VerifyChunks(left.c_str(), left.size(), right.c_str(), right.size(),
                 chunks, lcs2);
  }
}

TEST(LcsHybrid, TestOverflowInStatsComputations) {
  // We need a large number which creates negative numbers in case of int64_t
  // overflow.
  const int kMaxKeys = 5000000;
  std::vector<int> left(kMaxKeys, 0);
  for (int i = 0; i < kMaxKeys; i++) {
    left.push_back(i);
  }

  LcsOptions options;
  options.set_max_keys(kMaxKeys);
  options.set_lcs_bound_ratio(0.0f);
  LcsHybridEstimator estimator(left.size(), left.size(), &options);

  // Init stats
  KeyOccurrences right_occ;
  right_occ.Init(left.data(), left.size(), options.max_keys());
  LcsStats stats(left.data(), left.size(), right_occ);
  estimator.set_stats(&stats);

  int lower_bound, upper_bound;
  stats.DiffBounds(&lower_bound, &upper_bound);
  EXPECT_EQ(0, lower_bound);
  EXPECT_EQ(17500000, upper_bound);
}

}  // namespace internal
}  // namespace file_based_test_driver_base

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

#include "file_based_test_driver/base/lcs_myers.h"

#include <string.h>

#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "absl/random/random.h"
#include "file_based_test_driver/base/lcs.h"
#include "file_based_test_driver/base/lcs_test_util.h"

namespace file_based_test_driver_base {
namespace internal {

TEST(LcsMyers, Equal) {
  const char kLeft[] = "ababbaa";
  const char kRight[] = "ababbaa";
  std::vector<Chunk> chunks;
  LcsMyers<char> myers;
  int lcs =
      myers.Run(kLeft, strlen(kLeft), 0, kRight, strlen(kRight), 0, &chunks);
  EXPECT_EQ(7, lcs);
  EXPECT_EQ(1, chunks.size());
  EXPECT_TRUE(Equals(chunks[0], 0, 0, strlen(kLeft)));
}

TEST(LcsMyers, DeletionOnRightSide) {
  const char kLeft[] = "ababbaa";
  const char kRight[] = "ababaa";
  std::vector<Chunk> chunks;
  LcsMyers<char> myers;
  int lcs =
      myers.Run(kLeft, strlen(kLeft), 0, kRight, strlen(kRight), 0, &chunks);
  EXPECT_EQ(6, lcs);
  EXPECT_EQ(2, chunks.size());
  EXPECT_TRUE(Equals(chunks[0], 0, 0, 4));
  EXPECT_TRUE(Equals(chunks[1], 5, 4, 2));
}

TEST(LcsMyers, DeletionOnLeftSide) {
  const char kLeft[] = "abbbaa";
  const char kRight[] = "ababbaa";
  std::vector<Chunk> chunks;
  LcsMyers<char> myers;
  int lcs =
      myers.Run(kLeft, strlen(kLeft), 0, kRight, strlen(kRight), 0, &chunks);
  EXPECT_EQ(6, lcs);
  EXPECT_EQ(2, chunks.size());
  EXPECT_TRUE(Equals(chunks[0], 0, 0, 2));
  EXPECT_TRUE(Equals(chunks[1], 2, 3, 4));
}

TEST(LcsMyers, EmptySequence) {
  const char kLeft[] = "ababbaa";
  const char kRight[] = "";
  std::vector<Chunk> chunks;
  LcsMyers<char> myers;
  int lcs =
      myers.Run(kLeft, strlen(kLeft), 0, kRight, strlen(kRight), 0, &chunks);
  EXPECT_EQ(0, lcs);
  EXPECT_EQ(0, chunks.size());

  lcs = myers.Run(kRight, strlen(kRight), 0, kLeft, strlen(kLeft), 0, &chunks);
  EXPECT_EQ(0, lcs);
  EXPECT_EQ(0, chunks.size());
}

TEST(LcsMyers, RandomSequence) {
  absl::BitGen gen;
  LcsMyers<char> myers;
  for (int k = 0; k < 1000; k++) {
    std::string left =
        RandomString(gen, absl::Uniform<int>(gen, 0, 100), 'a', 'b');

    std::string right =
        RandomString(gen, absl::Uniform<int>(gen, 0, 100), 'a', 'b');
    std::vector<Chunk> chunks;
    int lcs  = RunSimpleLcs(left.c_str(), left.size(),
                            right.c_str(), right.size());
    int lcs2 = myers.Run(left.c_str(), left.size(), 0, right.c_str(),
                         right.size(), 0, &chunks);
    EXPECT_EQ(lcs, lcs2);
    VerifyChunks(left.c_str(), left.size(), right.c_str(), right.size(),
                 chunks, lcs2);
  }
}

}  // namespace internal
}  // namespace file_based_test_driver_base

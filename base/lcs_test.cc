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

#include "base/lcs.h"

#include <cstdlib>
#include <list>
#include <string>
#include <vector>

#include "base/logging.h"
#include "gtest/gtest.h"
#include "absl/random/random.h"
#include "absl/strings/string_view.h"
#include "base/lcs_test_util.h"

namespace file_based_test_driver_base {

template <class Container>
void CheckIntegerMap(const Container& left,
                     const Container& right,
                     const std::vector<int>& left_int,
                     const std::vector<int>& right_int) {
  // The mapping must not change the number of entries.
  EXPECT_EQ(left.size(), left_int.size());
  EXPECT_EQ(right.size(), right_int.size());

  // The integers must reproduce the equality properties of the original
  // entries. Just test all possible combinations.
  typename Container::const_iterator it = left.begin();
  for (int i = 0; i < left_int.size(); ++i, ++it) {
    typename Container::const_iterator jt = right.begin();
    for (int j = 0; j < right_int.size(); ++j, ++jt) {
      EXPECT_EQ(*it == *jt, left_int[i] == right_int[j]);
    }
  }
}

TEST(Lcs, MapToInteger) {
  std::vector<absl::string_view> left = {"line 1", "line 2", "line 3",
                                         "line 4"};
  std::vector<absl::string_view> right = {"line 2", "line 6", "line 4"};
  std::vector<int> left_int, right_int;
  int keys =
      Lcs::MapToInteger<absl::string_view>(left, right, &left_int, &right_int);
  EXPECT_EQ(4, keys);
  CheckIntegerMap(left, right, left_int, right_int);
}

TEST(Lcs, MapToInteger_SameInput) {
  std::vector<absl::string_view> left = {"line 1", "line 2", "line 3",
                                         "line 4"};
  std::vector<int> left_int, right_int;
  int keys =
      Lcs::MapToInteger<absl::string_view>(left, left, &left_int, &right_int);
  EXPECT_EQ(4, keys);
  CheckIntegerMap(left, left, left_int, right_int);
}

class LcsListTest : public testing::Test {
 public:
  void SetUp() override {
    left_lines_.push_back("line 1");
    left_lines_.push_back("line 2");
    left_lines_.push_back("line 3");

    right_lines_.push_back("line 2");
    right_lines_.push_back("line 4");

    // Convert arbitrary input into integer representation.
    keys_ = lcs_.MapToInteger<absl::string_view>(left_lines_, right_lines_,
                                                 &left_int_, &right_int_);
    CheckIntegerMap(left_lines_, right_lines_, left_int_, right_int_);
  }

  void TearDown() override {
    // Compute solution.
    std::vector<Chunk> chunks;
    int len = lcs_.Run(left_int_, right_int_, &chunks);
    // and verify it.
    // Only "line 2" is common to both ==> one chunk and lcs is 1.
    FILE_BASED_TEST_DRIVER_CHECK_EQ(1, len);
    FILE_BASED_TEST_DRIVER_CHECK_EQ(1, chunks.size());
  }

 protected:
  Lcs lcs_;
  std::list<std::string> left_lines_;
  std::list<std::string> right_lines_;
  std::vector<int> left_int_;
  std::vector<int> right_int_;
  int keys_;
};

TEST_F(LcsListTest, RunWithDefaultSettings) {
}

TEST_F(LcsListTest, RunWithMaxKeys) {
  // For performance reasons tell the LCS algorithm, how many
  // different keys (i.e. distinct lines) the input has. This is an optional
  // step.
  lcs_.mutable_options()->set_max_keys(keys_);
}

TEST_F(LcsListTest, RunWithLittleMemory) {
  lcs_.mutable_options()->set_max_memory(50);
}

TEST(Lcs, RunWithString) {
  const char kLeft[] = "this is the left string";
  const char kRight[] = "and this is the right string";
  // Lcs => "this is the " "t string"
  Lcs lcs;
  std::vector<Chunk> chunks;
  int len = lcs.Run(kLeft, kRight, &chunks);
  FILE_BASED_TEST_DRIVER_CHECK_EQ(2, chunks.size());
  absl::string_view common_string1("this is the ");
  absl::string_view common_string2("t string");
  FILE_BASED_TEST_DRIVER_CHECK_EQ(common_string1.size() + common_string2.size(), len);
  FILE_BASED_TEST_DRIVER_CHECK_EQ(common_string1,
           absl::string_view(kLeft + chunks[0].left, chunks[0].length));
  FILE_BASED_TEST_DRIVER_CHECK_EQ(common_string1,
           absl::string_view(kRight + chunks[0].right, chunks[0].length));
  FILE_BASED_TEST_DRIVER_CHECK_EQ(common_string2,
           absl::string_view(kLeft + chunks[1].left, chunks[1].length));
  FILE_BASED_TEST_DRIVER_CHECK_EQ(common_string2,
           absl::string_view(kRight + chunks[1].right, chunks[1].length));
}

TEST(Lcs, RunWithRandomStrings) {
  constexpr int kNumIterations = 10;
  constexpr int kStringSize = 10;
  absl::BitGen gen;
  for (int i = 0; i < kNumIterations; i++) {
    std::string left = internal::RandomString(gen, ' ', '~', kStringSize);
    std::string right = internal::RandomString(gen, ' ', '~', kStringSize);
    Lcs lcs;
    lcs.Run(left, right, nullptr);
  }
}

TEST(Lcs, RunWithVectorAsVector) {
  std::vector<int> left({1, 2, 3, 7, 8, 5, 6});
  std::vector<int> right({0, 1, 2, 3, 4, 5, 6});
  // Lcs => 1,2,3 and 5,6
  Lcs lcs;
  std::vector<Chunk> chunks;
  int len = lcs.Run(left, right, &chunks);
  FILE_BASED_TEST_DRIVER_CHECK_EQ(2, chunks.size());
  FILE_BASED_TEST_DRIVER_CHECK_EQ(5, len);
  for (Chunk& chunk : chunks) {
    CHECK_NE(0, chunk.length);
    for (int i = chunk.left; i < chunk.length; i++) {
      FILE_BASED_TEST_DRIVER_CHECK_EQ(right[chunk.right], left[chunk.left]);
    }
  }
}

TEST(Lcs, RunWithVectorAsPointer) {
  std::vector<int> left({1, 2, 3, 7, 8, 5, 6});
  std::vector<int> right({0, 1, 2, 3, 4, 5, 6});
  // Lcs => 1,2,3 and 5,6
  Lcs lcs;
  std::vector<Chunk> chunks;
  int len =
      lcs.Run(left.data(), left.size(), right.data(), right.size(), &chunks);
  FILE_BASED_TEST_DRIVER_CHECK_EQ(2, chunks.size());
  FILE_BASED_TEST_DRIVER_CHECK_EQ(5, len);
  for (Chunk& chunk : chunks) {
    CHECK_NE(0, chunk.length);
    for (int i = chunk.left; i < chunk.length; i++) {
      FILE_BASED_TEST_DRIVER_CHECK_EQ(right[chunk.right], left[chunk.left]);
    }
  }
}

}  // namespace file_based_test_driver_base

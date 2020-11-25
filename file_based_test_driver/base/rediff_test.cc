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

#include "file_based_test_driver/base/rediff.h"

#include <stddef.h>
#include <string.h>

#include <iterator>
#include <list>
#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace file_based_test_driver_base {

TEST(ProcessedEntryCreationTest, ProcessString) {
  const int kNumLines = 3;
  std::string kLines[kNumLines] = {"line one\n", "line two\n", "other stuff"};
  const std::string kTestInput1(absl::StrCat(kLines[0], kLines[1], kLines[2]));

  // kTestInput1 has last line "unterminated".
  std::list<ProcessedEntry> entries1;
  std::list<ProcessedEntry>::const_iterator iter;
  ASSERT_EQ(kNumLines, ProcessedEntry::ProcessString(kTestInput1, &entries1));
  iter = entries1.begin();
  for (int i = 0; i < kNumLines; ++i, ++iter) {
    SCOPED_TRACE(absl::StrCat("i == ", i));
    EXPECT_EQ(kLines[i].size(), iter->length);
    EXPECT_EQ(0, strncmp(iter->data, kLines[i].data(), kLines[i].size()));
  }
  EXPECT_TRUE(iter == entries1.end());

  // kTestInput2 terminates the last line; expect the same results.
  const std::string kTestInput2(absl::StrCat(kTestInput1, "\n"));
  kLines[kNumLines-1] += "\n";
  std::list<ProcessedEntry> entries2;
  ASSERT_EQ(kNumLines, ProcessedEntry::ProcessString(kTestInput2, &entries2));
  iter = entries2.begin();
  for (int i = 0; i < kNumLines; ++i, ++iter) {
    SCOPED_TRACE(absl::StrCat("i == ", i));
    EXPECT_EQ(kLines[i].size(), iter->length);
    EXPECT_EQ(0, strncmp(iter->data, kLines[i].data(), kLines[i].size()));
  }
  EXPECT_TRUE(iter == entries2.end());
}

TEST(ProcessedEntryCreationTest, ProcessVector) {
  const int kNumLines = 3;
  const std::string kLines[kNumLines] = {"line one", "line two", "other stuff"};
  std::vector<const char*> v;
  for (int i = 0; i < kNumLines; ++i)
    v.push_back(kLines[i].c_str());

  std::list<ProcessedEntry> entries;
  ASSERT_EQ(kNumLines, ProcessedEntry::ProcessVector(
                           v, &entries, ProcessedEntry::DefaultScoreMatrix()));
  std::list<ProcessedEntry>::const_iterator iter = entries.begin();
  for (int i = 0; i < kNumLines; ++i, ++iter) {
    SCOPED_TRACE(absl::StrCat("i == ", i));
    EXPECT_EQ(kLines[i].size(), iter->length);
    // Test pointer equality: we expect the pointers to be equal, thus
    // indicating that we did not do unexpected string copies.
    EXPECT_EQ(kLines[i].c_str(), iter->data);
  }
  EXPECT_TRUE(iter == entries.end());
}

TEST(ProcessedEntryCreationTest, ProcessVectorOfStrings) {
  const int kNumLines = 3;
  const std::string kLines[kNumLines] = {"line one", "line two", "other stuff"};
  std::vector<std::string> v;
  for (int i = 0; i < kNumLines; ++i)
    v.push_back(kLines[i]);

  std::list<ProcessedEntry> entries;
  ASSERT_EQ(kNumLines, ProcessedEntry::ProcessVectorOfStrings(
                           v, &entries, ProcessedEntry::DefaultScoreMatrix()));
  std::list<ProcessedEntry>::const_iterator iter = entries.begin();
  for (int i = 0; i < kNumLines; ++i, ++iter) {
    SCOPED_TRACE(absl::StrCat("i == ", i));
    EXPECT_EQ(v[i].size(), iter->length);
    // Test pointer equality: we expect the pointers to be equal, thus
    // indicating that we did not do unexpected string copies.
    EXPECT_EQ(v[i].c_str(), iter->data);
  }
  EXPECT_TRUE(iter == entries.end());
}

TEST(ProcessedEntryCreationTest, ProcessVectorOfStringViews) {
  const int kNumLines = 3;
  const std::string kLines[kNumLines] = {"line one", "line two", "other stuff"};
  std::vector<absl::string_view> v;
  for (int i = 0; i < kNumLines; ++i)
    v.push_back(kLines[i]);

  std::list<ProcessedEntry> entries;
  ASSERT_EQ(kNumLines, ProcessedEntry::ProcessVectorOfStringViews(
                           v, &entries, ProcessedEntry::DefaultScoreMatrix()));
  std::list<ProcessedEntry>::const_iterator iter = entries.begin();
  for (int i = 0; i < kNumLines; ++i, ++iter) {
    SCOPED_TRACE(absl::StrCat("i == ", i));
    EXPECT_EQ(v[i].size(), iter->length);
    // Test pointer equality: we expect the pointers to be equal, thus
    // indicating that we did not do unexpected string copies.
    EXPECT_EQ(v[i].data(), iter->data);
  }
  EXPECT_TRUE(iter == entries.end());
}

TEST(ProcessedEntryCreationTest, NullByteCharacters) {
  std::vector<std::string> lines;
  // Create two strings which differ, but only when comparison doesn't stop at
  // the '\0' character.
  lines.push_back(std::string("ab\0d", 4));
  lines.push_back(std::string("ab\0e", 4));

  std::list<ProcessedEntry> entries;
  ASSERT_EQ(lines.size(),
            ProcessedEntry::ProcessVectorOfStrings(
                lines, &entries, ProcessedEntry::DefaultScoreMatrix()));
  // First and last entry must be different if the comparison works correctly.
  EXPECT_NE(*entries.begin(), *entries.rbegin());
}

class ReDiffTest : public testing::Test {
 public:
  void SetUp() override {
    d = new ReDiff();
    d->set_tolerance(0);
  }

  void TearDown() override { delete d; }
  // Helper function - compare d->ChunksToString() with the argument
  void DiffCheck(const std::string& expected) {
    std::string result;
    d->ChunksToString(&result);
    EXPECT_EQ(expected, result);
  }
  // Helper to call DiffStrings on s1/s2
  // We change all spaces to \n.  The differ splits on \n, but
  // using spaces makes the tests themselves more readable.
  void diff(std::string s1, std::string s2) {
    for (int i = 0; i < s1.size(); ++i) {
      if (s1[i] == ' ') s1[i] = '\n';
    }
    for (int i = 0; i < s2.size(); ++i) {
      if (s2[i] == ' ') s2[i] = '\n';
    }
    d->DiffStrings(s1, s2);
  }

  ReDiff* d;
};

TEST_F(ReDiffTest, EmptyTest) {
  diff("", "");
  DiffCheck("");
}

TEST_F(ReDiffTest, SimpleTest) {
  diff("abc", "abc");
  DiffCheck("equal 0 1 0 1\n");
  diff("a b c ", "a b c ");
  DiffCheck("equal 0 3 0 3\n");
}

TEST_F(ReDiffTest, AddTest) {
  // Add in the middle
  diff("a b c ", "a FOO b c ");
  DiffCheck("equal 0 1 0 1\n"
            "insert 0 0 1 2\n"
            "equal 1 3 2 4\n");
  // Add at the beginning
  diff("a b ", "FOO a b ");
  DiffCheck("insert 0 0 0 1\n"
            "equal 0 2 1 3\n");
  // Add at the end
  diff("a b ", "a b FOO ");
  DiffCheck("equal 0 2 0 2\n"
            "insert 0 0 2 3\n");
}

TEST_F(ReDiffTest, DeleteTest) {
  // Delete in the middle
  diff("a b c ", "a c ");
  DiffCheck("equal 0 1 0 1\n"
            "delete 1 2 0 0\n"
            "equal 2 3 1 2\n");
  // Delete from the front
  diff("a b c ", "b c ");
  DiffCheck("delete 0 1 0 0\n"
            "equal 1 3 0 2\n");
  // Delete from the back
  diff("a b c ", "a b ");
  DiffCheck("equal 0 2 0 2\n"
            "delete 2 3 0 0\n");
}

TEST_F(ReDiffTest, ReplaceTest) {
  // Replace at the beginning
  diff("a b c ", "foo bar b c ");
  DiffCheck("replace 0 1 0 2\n"
            "equal 1 3 2 4\n");
  // Replace in the middle
  diff("a b c ", "a foo bar c ");
  DiffCheck("equal 0 1 0 1\n"
            "replace 1 2 1 3\n"
            "equal 2 3 3 4\n");
  // Replace at the end
  diff("a b c ", "a b foo bar ");
  DiffCheck("equal 0 2 0 2\n"
            "replace 2 3 2 4\n");
}

TEST_F(ReDiffTest, ScoreMatrixTestArray) {
  d->set_tolerance(5);
  // With this tolerance and default scoring, the matching "d" gets
  // ignored.
  diff("a b c x d y ", "a b c X d Y ");
  DiffCheck("equal 0 3 0 3\n"
            "replace 3 6 3 6\n");

  int score_matrix[256];
  for (int i = 0; i < 256; ++i) {
    score_matrix[i] = 6;
  }

  d->set_score_matrix(score_matrix);
  // Now different results: the d has enough information to be a match.
  diff("a b c x d y ", "a b c X d Y ");
  DiffCheck("equal 0 3 0 3\n"
            "replace 3 4 3 4\n"
            "equal 4 5 4 5\n"
            "replace 5 6 5 6\n");
}

TEST_F(ReDiffTest, ScoreMatrixTestArrayBackport) {
  d->set_tolerance(5);
  // With this tolerance and default scoring, the matching "d" gets
  // ignored.
  diff("a b c x d y ", "a b c X d Y ");
  DiffCheck("equal 0 3 0 3\n"
            "replace 3 6 3 6\n");

  int score_matrix[256];
  for (int i = 0; i < 256; ++i) {
    score_matrix[i] = 6;
  }

  // Take ownership is no longer used, but needs to be maintained for backwards
  // compatibility.
  d->set_score_matrix(score_matrix, /*take_ownership=*/false);
  // Now different results: the d has enough information to be a match.
  diff("a b c x d y ", "a b c X d Y ");
  DiffCheck("equal 0 3 0 3\n"
            "replace 3 4 3 4\n"
            "equal 4 5 4 5\n"
            "replace 5 6 5 6\n");
}

TEST_F(ReDiffTest, ScoreMatrixTestVector) {
  d->set_tolerance(5);
  // With this tolerance and default scoring, the matching "d" gets
  // ignored.
  diff("a b c x d y ", "a b c X d Y ");
  DiffCheck("equal 0 3 0 3\n"
            "replace 3 6 3 6\n");

  std::vector<int> score_vec(256, 6);
  score_vec[0] = 0;
  d->set_score_matrix(score_vec);
  diff("a b c x d y ", "a b c X d Y ");
  DiffCheck("equal 0 3 0 3\n"
            "replace 3 4 3 4\n"
            "equal 4 5 4 5\n"
            "replace 5 6 5 6\n");
}



TEST_F(ReDiffTest, LowScoringRegionTest) {
  d->set_tolerance(1);
  // Verify that a string of equivalent lines with "informationless"
  // characters (like blank comment lines) don't get matched if there
  // is nothing to anchor them, even with a minimal tolerance threshold.
  diff("a b c /** * */ { d } # // a b c ",
       "x y z /** * */ { h } # // x y z ");
  DiffCheck("replace 0 14 0 14\n");

  // With tolerance of -1, the story changes
  d->set_tolerance(-1);
  diff("a b c /** * */ { d } # // a b c ",
       "x y z /** * */ { h } # // x y z ");
  DiffCheck("replace 0 3 0 3\n"
            "equal 3 7 3 7\n"
            "replace 7 8 7 8\n"
            "equal 8 11 8 11\n"
            "replace 11 14 11 14\n");

  // And a similar setup but with real characters is different.
  d->set_tolerance(1);
  diff("a b c q r s t d u v w",
       "x y z q r s t h u v w");
  DiffCheck("replace 0 3 0 3\n"
            "equal 3 7 3 7\n"
            "replace 7 8 7 8\n"
            "equal 8 11 8 11\n");

  // In fact, just a single anchoring character with a non-zero
  // score is all you need to make the match happen.
  diff("a b c M * */ { d N # // a b c ",
       "x y z M * */ { h N # // x y z ");
  DiffCheck("replace 0 3 0 3\n"
            "equal 3 7 3 7\n"
            "replace 7 8 7 8\n"
            "equal 8 11 8 11\n"
            "replace 11 14 11 14\n");

  // ...even in the middle of a region.
  // We expand the region to capture all matches.
  diff("a b c /** M */ { d } N // a b c ",
       "x y z /** M */ { h } N // x y z ");
  DiffCheck("replace 0 3 0 3\n"
            "equal 3 7 3 7\n"
            "replace 7 8 7 8\n"
            "equal 8 11 8 11\n"
            "replace 11 14 11 14\n");

  // ...and also at the end.
  diff("a b c /** * */ M d N # // a b c ",
       "x y z /** * */ M h N # // x y z ");
  DiffCheck("replace 0 3 0 3\n"
            "equal 3 7 3 7\n"
            "replace 7 8 7 8\n"
            "equal 8 11 8 11\n"
            "replace 11 14 11 14\n");
}

TEST_F(ReDiffTest, LeadingAndTrailingMatchHasNoInformation) {
  // A single opening or closing brace is considered informationless,
  // and a matching region containing only that brace gets discarded.
  diff("x { y } z ",
       "X { Y } Z ");
  DiffCheck("replace 0 5 0 5\n");
  // However, an opening/closing brace at the very beginning or very end
  // (our leading/trailing greedy matches) never get discarded.
  diff("{ x } ",
       "{ X } ");
  DiffCheck("equal 0 1 0 1\n"
            "replace 1 2 1 2\n"
            "equal 2 3 2 3\n");
}

class RediffInternalsTest : public testing::Test {
 public:
  typedef ReDiff::LPEit LPEit;
  void SetUp() override { diff_ = new ReDiff(); }
  void TearDown() override { delete diff_; }

  void ResetIterators() {
    left_begin_ = diff_->left_list_.begin();
    left_end_ = diff_->left_list_.end();
    right_begin_ = diff_->right_list_.begin();
    right_end_ = diff_->right_list_.end();
  }

  // Set the list and size fields in our diff.
  // Takes string pointers so that the strings are not temporary;
  // the ProcessedEntry() interface stores pointers into its arguments.
  void PrepareLists(std::string* s1, std::string* s2) {
    for (int i = 0; i < s1->size(); ++i) {
      if ((*s1)[i] == ' ') (*s1)[i] = '\n';
    }
    for (int i = 0; i < s2->size(); ++i) {
      if ((*s2)[i] == ' ') (*s2)[i] = '\n';
    }
    diff_->left_list_.clear();
    diff_->right_list_.clear();
    diff_->left_size_ = ProcessedEntry::ProcessString(*s1, &diff_->left_list_);
    diff_->right_size_ = ProcessedEntry::ProcessString(*s2,
                                                       &diff_->right_list_);
    // Prepend/append pads to these lists.
    diff_->left_list_.push_back(diff_->null_entry_);
    diff_->left_list_.push_front(diff_->null_entry_);
    diff_->right_list_.push_back(diff_->null_entry_);
    diff_->right_list_.push_front(diff_->null_entry_);
    diff_->left_size_ += 2;
    diff_->right_size_ += 2;

    ResetIterators();
  }

  // Call private functions in ReDiff (since we're a friend).
  int ProcessLeadingMatches(LPEit* l, LPEit* r) {
    return diff_->ProcessLeadingMatches(l, r);
  }
  int ProcessTrailingMatches(LPEit* l, LPEit* r, int leading_matches) {
    return diff_->ProcessTrailingMatches(l, r, leading_matches);
  }
  ReDiff* diff_;
  LPEit left_begin_;
  LPEit left_end_;
  LPEit right_begin_;
  LPEit right_end_;
};

TEST_F(RediffInternalsTest, EqualEntries) {
  std::string first("a b c ");
  std::string second("a b c ");
  PrepareLists(&first, &second);
  EXPECT_EQ(3, ProcessLeadingMatches(&left_begin_, &right_begin_));
  // Should be pointing at a trailing pad.  Iterators should be
  // dereferenceable.
  EXPECT_TRUE(left_begin_->data == NULL);
  EXPECT_TRUE(right_begin_->data == NULL);
  EXPECT_FALSE(*left_begin_ == *right_begin_);
  // Advancing once should hit the ends.
  ++left_begin_;
  ++right_begin_;
  EXPECT_TRUE(left_begin_ == left_end_);
  EXPECT_TRUE(right_begin_ == right_end_);
}

TEST_F(RediffInternalsTest, OverlappingMatches) {
  std::string first("a b a ");
  std::string second("a ");
  PrepareLists(&first, &second);
  // We have 1 line of leading or trailing match.
  EXPECT_EQ(1, ProcessLeadingMatches(&left_begin_, &right_begin_));
  ResetIterators();
  EXPECT_EQ(1, ProcessTrailingMatches(&left_end_, &right_end_, 0));
  ResetIterators();

  // But if we provide the value from ProcessLeadingMatches() to
  // ProcessTrailingMatches, it refuses to let them overlap.
  EXPECT_EQ(1, ProcessLeadingMatches(&left_begin_, &right_begin_));
  EXPECT_EQ(0, ProcessTrailingMatches(&left_end_, &right_end_, 1));
  // Everything has been processed on the right side: should have an
  // empty range.
  EXPECT_TRUE(right_begin_ == right_end_);
}

TEST_F(RediffInternalsTest, NonOverlappingMatches) {
  std::string first("a b x y d e f ");
  std::string second("a b X Y d e f ");
  PrepareLists(&first, &second);
  EXPECT_EQ(2, ProcessLeadingMatches(&left_begin_, &right_begin_));
  EXPECT_EQ(3, ProcessTrailingMatches(&left_end_, &right_end_, 2));

  EXPECT_EQ("x\n", std::string(left_begin_->data, left_begin_->length));
  EXPECT_EQ("X\n", std::string(right_begin_->data, right_begin_->length));
  EXPECT_EQ("d\n", std::string(left_end_->data, left_end_->length));
  EXPECT_EQ("d\n", std::string(right_end_->data, right_end_->length));
  // Distance between begin and end should be 2.
  ++left_begin_;
  ++left_begin_;
  EXPECT_TRUE(left_begin_ == left_end_);
  ++right_begin_;
  ++right_begin_;
  EXPECT_TRUE(right_begin_ == right_end_);
}

}  // namespace file_based_test_driver_base

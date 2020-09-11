//
// Copyright 2020 Google LLC
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
// IMPORTANT: When modifying these tests, be extremely mindful that the
// hard-coded diffs below act as regression tests to ensure the backwards
// compatibility of this library. If you're adding a
// cool new feature or fixing a bug or something, it's best to leave the
// existing tests alone. We're targeting a (vaguely) standardized format here.
// Anyone who generated a diff five years ago should be able to patch with that
// diff five years from now.

#include "base/unified_diff_oss.h"

#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"

namespace file_based_test_driver_base {

using ::testing::Eq;
using ::testing::Pointwise;

static UnifiedDiffOptions MakeOptions(int context_size) {
  return UnifiedDiffOptions().set_context_size(context_size);
}

TEST(UnifiedDiffTest, EmptyStrings) {
  ASSERT_EQ("", UnifiedDiff("", "", "foo", "bar", MakeOptions(3)));
}

TEST(UnifiedDiffTest, SameContent) {
  std::string ls("a\nb\nb2\n");
  std::string rs("a\nb\nb2\n");

  ASSERT_EQ("", UnifiedDiff(ls, rs, "foo", "bar", MakeOptions(3)));
}

TEST(UnifiedDiffTest, OneLineChange1) {
  std::string ls("applesauce\n");
  std::string rs("bubbletea\n");

  ASSERT_EQ(
      "--- foo\n"
      "+++ bar\n"
      "@@ -1 +1 @@\n"
      "-applesauce\n"
      "+bubbletea\n",
      UnifiedDiff(ls, rs, "foo", "bar", MakeOptions(3)));
}

TEST(UnifiedDiffTest, OneLineChange2) {
  std::string ls("applesauce\n");
  std::string rs("bubbletea\nasdf\nbeep\n");

  ASSERT_EQ(
      "--- foo\n"
      "+++ bar\n"
      "@@ -1 +1,3 @@\n"
      "-applesauce\n"
      "+bubbletea\n"
      "+asdf\n"
      "+beep\n",
      UnifiedDiff(ls, rs, "foo", "bar", MakeOptions(3)));
}

TEST(UnifiedDiffTest, OneLineChange3) {
  std::string ls("applesauce\norganic\nplastic\n");
  std::string rs("bubbletea\n");

  ASSERT_EQ(
      "--- foo\n"
      "+++ bar\n"
      "@@ -1,3 +1 @@\n"
      "-applesauce\n"
      "-organic\n"
      "-plastic\n"
      "+bubbletea\n",
      UnifiedDiff(ls, rs, "foo", "bar", MakeOptions(3)));
}

TEST(UnifiedDiffTest, OneLineAdd) {
  std::string ls("");
  std::string rs("bubbletea\n");

  ASSERT_EQ(
      "--- foo\n"
      "+++ bar\n"
      "@@ -0,0 +1 @@\n"
      "+bubbletea\n",
      UnifiedDiff(ls, rs, "foo", "bar", MakeOptions(3)));
}

TEST(UnifiedDiffTest, OneLineDelete) {
  std::string ls("applesauce\n");
  std::string rs("");

  ASSERT_EQ(
      "--- foo\n"
      "+++ bar\n"
      "@@ -1 +0,0 @@\n"
      "-applesauce\n",
      UnifiedDiff(ls, rs, "foo", "bar", MakeOptions(3)));
}

TEST(UnifiedDiffTest, AllRemoved) {
  std::string ls("a\nb\nb2\n");
  std::string rs("");

  ASSERT_EQ(
      "--- foo\n"
      "+++ bar\n"
      "@@ -1,3 +0,0 @@\n"
      "-a\n"
      "-b\n"
      "-b2\n",
      UnifiedDiff(ls, rs, "foo", "bar", MakeOptions(3)));
}

TEST(UnifiedDiffTest, AllAdded) {
  std::string ls("");
  std::string rs("a\nb\nb2\n");

  ASSERT_EQ(
      "--- foo\n"
      "+++ bar\n"
      "@@ -0,0 +1,3 @@\n"
      "+a\n"
      "+b\n"
      "+b2\n",
      UnifiedDiff(ls, rs, "foo", "bar", MakeOptions(3)));
}

TEST(UnifiedDiffTest, AddedInContext) {
  std::string ls("d\nd\n");
  std::string rs("d\na\nb\nb2\nd\n");

  ASSERT_EQ(
      "--- foo\n"
      "+++ bar\n"
      "@@ -1,2 +1,5 @@\n"
      " d\n"
      "+a\n"
      "+b\n"
      "+b2\n"
      " d\n",
      UnifiedDiff(ls, rs, "foo", "bar", MakeOptions(3)));
}

TEST(UnifiedDiffTest, AddedDifferentContextSizes) {
  std::string ls("1\n2\n3\n4\nd\nd\n");
  std::string rs("1\n2\n3\n4\nd\na\nb\nb2\nd\n");

  ASSERT_EQ(
      "--- foo\n"
      "+++ bar\n"
      "@@ -4,3 +4,6 @@\n"
      " 4\n"
      " d\n"
      "+a\n"
      "+b\n"
      "+b2\n"
      " d\n",
      UnifiedDiff(ls, rs, "foo", "bar", MakeOptions(2)));
  ASSERT_EQ(
      "--- foo\n"
      "+++ bar\n"
      "@@ -3,4 +3,7 @@\n"
      " 3\n"
      " 4\n"
      " d\n"
      "+a\n"
      "+b\n"
      "+b2\n"
      " d\n",
      UnifiedDiff(ls, rs, "foo", "bar", MakeOptions(3)));
}

TEST(UnifiedDiffTest, NoNewlineAtEnd) {
  std::string ls("d\nd");
  std::string rs("d\nc");

  ASSERT_EQ(
      "--- foo\n"
      "+++ bar\n"
      "@@ -1,2 +1,2 @@\n"
      " d\n"
      "-d\n"
      "\\ No newline at end of file\n"
      "+c\n"
      "\\ No newline at end of file\n",
      UnifiedDiff(ls, rs, "foo", "bar", MakeOptions(3)));
}

TEST(UnifiedDiffTest, ReplacedInContext) {
  std::string ls("d\nF\nd\n");
  std::string rs("d\na\nb\nb2\nd\n");

  ASSERT_EQ(
      "--- foo\n"
      "+++ bar\n"
      "@@ -1,3 +1,5 @@\n"
      " d\n"
      "-F\n"
      "+a\n"
      "+b\n"
      "+b2\n"
      " d\n",
      UnifiedDiff(ls, rs, "foo", "bar", MakeOptions(3)));
}

TEST(UnifiedDiffTest, ComplexDiff) {
  std::string ls("a\nb\nb1\nc\nc\nc\nc\nc\nc\nc\nd\nx\nfoo\nd\nd\nd\n");
  std::string rs("d\nb\nb2\nc\nc\nc\nc\nc\nc\nc\nd\nd\nx\nd\nd\nd\n");

  ASSERT_EQ(
      "--- foo\n"
      "+++ bar\n"
      "@@ -1,6 +1,6 @@\n"
      "-a\n"
      "+d\n"
      " b\n"
      "-b1\n"
      "+b2\n"
      " c\n"
      " c\n"
      " c\n"
      "@@ -8,9 +8,9 @@\n"
      " c\n"
      " c\n"
      " c\n"
      "+d\n"
      " d\n"
      " x\n"
      "-foo\n"
      " d\n"
      " d\n"
      " d\n",
      UnifiedDiff(ls, rs, "foo", "bar", MakeOptions(3)));
}

TEST(UnifiedDiffTest, AnsiColorizer) {
  std::string ls("d\nc\nd\n");
  std::string rs("d\na\nb\nb2\nd\n");

  UnifiedDiffOptions options;
  options.set_context_size(3);
  options.set_colorizer(UnifiedDiffColorizer::AnsiColorizer());

  ASSERT_EQ(
      "--- foo\n"
      "+++ bar\n"
      "@@ -1,3 +1,5 @@\n"
      " d\n"
      "\033[31m-c\n\033[0m"
      "\033[32m+a\n"
      "+b\n"
      "+b2\n\033[0m"
      " d\n",
      UnifiedDiff(ls, rs, "foo", "bar", options));
}

TEST(UnifiedDiffTest, CustomColorizer) {
  std::string ls("d\nc\nd\n");
  std::string rs("d\na\nb\nb2\nd\n");

  UnifiedDiffColorizer colorizer("BEGIN_ADD", "END_ADD", "BEGIN_DEL", "END_DEL",
                                 nullptr);

  UnifiedDiffOptions options;
  options.set_context_size(3);
  options.set_colorizer(&colorizer);

  ASSERT_EQ(
      "--- foo\n"
      "+++ bar\n"
      "@@ -1,3 +1,5 @@\n"
      " d\n"
      "BEGIN_DEL-c\nEND_DEL"
      "BEGIN_ADD+a\n"
      "+b\n"
      "+b2\nEND_ADD"
      " d\n",
      UnifiedDiff(ls, rs, "foo", "bar", options));
}

TEST(UnifiedDiffTest, MissingNewlineWarnings) {
  UnifiedDiffOptions options;
  options.set_context_size(3);

  std::string ls("d\nd\n");
  std::string rs("d\na\nb\nb2\nd");

  options.set_warn_missing_eof_newline(true);
  ASSERT_EQ(
      "--- foo\n"
      "+++ bar\n"
      "@@ -1,2 +1,5 @@\n"
      " d\n"
      "-d\n"
      "+a\n"
      "+b\n"
      "+b2\n"
      "+d\n"
      "\\ No newline at end of file\n",
      UnifiedDiff(ls, rs, "foo", "bar", options));

  options.set_warn_missing_eof_newline(false);
  ASSERT_EQ(
      "--- foo\n"
      "+++ bar\n"
      "@@ -1,2 +1,5 @@\n"
      " d\n"
      "-d\n"
      "+a\n"
      "+b\n"
      "+b2\n"
      "+d\n",
      UnifiedDiff(ls, rs, "foo", "bar", options));
}

}  // namespace file_based_test_driver_base

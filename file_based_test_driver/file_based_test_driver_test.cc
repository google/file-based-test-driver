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

#include "file_based_test_driver/file_based_test_driver.h"

#include <stdlib.h>

#include <cstdint>
#include <functional>
#include <initializer_list>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/flags/declare.h"
#include "absl/flags/flag.h"
#include "absl/functional/bind_front.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/log/log_entry.h"
#include "absl/log/log_sink.h"
#include "absl/log/log_sink_registry.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "file_based_test_driver/base/file_util.h"
#include "file_based_test_driver/base/status_matchers.h"
#include "file_based_test_driver/run_test_case_result.h"
#include "file_based_test_driver/test_case_mode.h"

ABSL_DECLARE_FLAG(int32_t, file_based_test_driver_insert_leading_blank_lines);
ABSL_DECLARE_FLAG(bool, file_based_test_driver_generate_test_output);
ABSL_DECLARE_FLAG(bool, file_based_test_driver_individual_tests);
ABSL_DECLARE_FLAG(bool, file_based_test_driver_log_ignored_test);

using ::absl_testing::StatusIs;
using ::testing::ElementsAre;
using ::testing::IsEmpty;
using ::testing::SizeIs;

// A replacement for VarSetter that works with new-style flags as well as
// old-style flags.
class FlagSetter {
 public:
  template <typename T, typename U>
  FlagSetter(T* flag, U new_value) {
    const auto old_value = absl::GetFlag(*flag);
    restorer_ = [flag, old_value] { absl::SetFlag(flag, old_value); };
    absl::SetFlag(flag, new_value);
  }
  ~FlagSetter() { restorer_(); }
  FlagSetter(const FlagSetter&) = delete;
  FlagSetter& operator=(const FlagSetter&) = delete;

 private:
  std::function<void()> restorer_;
};

// If we expect that a test will pass, ensure that more debugging information is
// collected by sponge in the event of a failure by setting the flag to cause
// googletest expectations within the testing. If we expect that it will fail,
// ensure that googletest expectations are not verified, because the failure of
// the test-under-test is expected.
#define EXPECT_TEST_PASSES(test)                                        \
  {                                                                     \
    FlagSetter v(&FLAGS_file_based_test_driver_individual_tests, true); \
    EXPECT_TRUE(test);                                                  \
  }
#define EXPECT_TEST_FAILS(test)                                          \
  {                                                                      \
    FlagSetter v(&FLAGS_file_based_test_driver_individual_tests, false); \
    EXPECT_FALSE(test);                                                  \
  }

static std::string GetWorkspace() {
  return std::string(getenv("TEST_WORKSPACE"));
}

namespace file_based_test_driver {

using ::testing::ContainerEq;

using TestdataUtilCallbackTest = ::testing::Test;

static std::string StringifyResultDiff(const ResultDiff& result_diff) {
  return absl::Substitute(
      R"(FILE:$0:$4
EXPECTED:
$1
ACTUAL:
$2
DIFF:
$3)",
      result_diff.file_path, result_diff.expected, result_diff.actual,
      result_diff.unified_diff, result_diff.start_line_number);
}

// Extracts a single test case from testdata starting at '*start_line' using
// GetNextTestCase(). Verifies that the results match 'expected_end_line',
// 'expected_parts' and 'expected_comments'. If 'expected_comments' is empty,
// then the function verifies that all comments are empty. Also verifies that
// BuildTestFileEntry is the exact inverse of GetNextTestData for the test case
// that was read (unless manual_expected_reassembled is specified explicitly,
// which it must be for cases with unnecessary escaping that gets removed).
void VerifyGetNextTestdata(
    const std::string& testdata, int start_line, int expected_end_line,
    const std::vector<std::string>& expected_parts,
    const std::vector<internal::TestCasePartComments>& expected_comments,
    const std::string& manual_expected_reassembled = "") {
  std::vector<std::string> lines = internal::SplitTestFileData(testdata);

  // Verify GetNextTestData.
  int line_number = start_line;
  std::vector<std::string> parts;
  std::vector<internal::TestCasePartComments> comments;
  FILE_BASED_TEST_DRIVER_EXPECT_OK(internal::GetNextTestCase(lines, &line_number, &parts, &comments));
  EXPECT_EQ(expected_end_line, line_number);
  EXPECT_THAT(parts, ContainerEq(expected_parts));
  if (!expected_comments.empty()) {
    EXPECT_THAT(comments, ContainerEq(expected_comments));
  } else {
    const std::vector<internal::TestCasePartComments> expected_empty_comments(
        expected_parts.size(), {"", ""});
    EXPECT_THAT(comments, ContainerEq(expected_empty_comments));
  }

  // Test reassembly.
  std::string reassembled = internal::BuildTestFileEntry(parts, comments);
  if (lines[line_number - 1] == "==") {
    --line_number;
  }
  std::vector<std::string> expected_lines(lines.begin() + start_line,
                                          lines.begin() + line_number);
  std::string expected_reassembled =
      absl::StrCat(absl::StrJoin(expected_lines, "\n"), "\n");

  if (manual_expected_reassembled.empty()) {
    EXPECT_EQ(expected_reassembled, reassembled);
  } else {
    EXPECT_NE(expected_reassembled, reassembled);
    EXPECT_EQ(manual_expected_reassembled, reassembled);
  }
}

TEST(TestdataUtilTest, Basic) {
  VerifyGetNextTestdata(
      "Part 1\n"
      "--\n"
      "Part 2\n",
      0, 3, {"Part 1\n", "Part 2\n"} /* expected_parts */,
      {} /* no expected comments */);

  VerifyGetNextTestdata(
      "Part 1\n"
      "--\n"
      "Part 2\n"
      "--\n"
      "Part 3\n",
      0, 5, {"Part 1\n", "Part 2\n", "Part 3\n"} /* expected_parts */,
      {} /* expect no comments */);
}

TEST(TestdataUtilTest, MultipleSections) {
  std::string testdata =
      "Test 1-Part 1\n"
      "--\n"
      "Test 1-Part 2\n"
      "==\n"
      "Test 2-Part 1\n"
      "--\n"
      "Test 2-Part 2\n"
      "--\n"
      "Test 2-Part 3\n";

  VerifyGetNextTestdata(
      testdata, 0, 4,
      {"Test 1-Part 1\n", "Test 1-Part 2\n"} /* expected_parts */,
      {} /* expect no comments */);

  VerifyGetNextTestdata(testdata, 4, 9,
                        {"Test 2-Part 1\n", "Test 2-Part 2\n",
                         "Test 2-Part 3\n"} /* expected_parts */,
                        {} /* expect no comments */);
}

TEST(TestdataUtilTest, Comment) {
  std::string testdata =
      "# Start Comment 1\n"
      "# Start Comment 2\n"
      "\\# not a \\#comment\n"
      "Line 1\n"
      " foo # not a comment\n"
      "# not a comment\n"
      " # not a comment\n"
      "\\# not a comment\n"
      "# End Comment 1\n"
      "# End Comment 2\n";

  VerifyGetNextTestdata(
      testdata, 0, 10,
      {"# not a \\#comment\n"
       "Line 1\n"
       " foo # not a comment\n"
       "# not a comment\n"
       " # not a comment\n"
       "# not a comment\n"} /* expected_parts */,
      {{"# Start Comment 1\n# Start Comment 2\n",
        "# End Comment 1\n# End Comment 2\n"}} /* expected_comments */);

  // Escaped comments in the middle are unescaped.
  VerifyGetNextTestdata(
      "\\# Test body 1\n"
      "\\# Test body 2\n"
      "# Test body 3\n"
      "\\# Test body 4\n",
      0, 4,
      {"# Test body 1\n"
       "# Test body 2\n"
       "# Test body 3\n"
       "# Test body 4\n"} /* expected_parts */,
      {{}} /* expected_comments */,
      "\\# Test body 1\n"
      "# Test body 2\n"
      "# Test body 3\n"
      "\\# Test body 4\n" /* manual_expected_reassembled */);
}

TEST(TestdataUtilTest, EmptyLines) {
  // Test empty line, escape handling
  std::string testdata =
      "# Start Comment 1\n"
      "\n"  // empty line in start comment
      "# Start Comment 2\n"
      "\n"    // empty line, part of start comment
      "\\\n"  // escaped empty line, first line of test case
      "\n"    // not-escaped empty line, also part of test case
      "Line 1\n"
      "\n"  // not-escaped empty line, part of test case
      "Line 2\n"
      "\n"  // two not-escaped empty lines, part of test case
      "\n"
      "Line 3\n"
      "\n"    // not-escaped empty line, part of test case
      "\\\n"  // escaped empty line, last line of test case
      "\n"    // empty line, part of end comment
      "# End Comment 1\n"
      "\n"  // empty line in end comment
      "# End Comment 2\n";

  VerifyGetNextTestdata(
      testdata, 0, 18,
      {"\n"
       "\n"
       "Line 1\n"
       "\n"
       "Line 2\n"
       "\n"
       "\n"
       "Line 3\n"
       "\n"
       "\n"} /* expected_parts */,
      {{"# Start Comment 1\n\n# Start Comment 2\n\n",
        "\n# End Comment 1\n\n# End Comment 2\n"}} /* expected_comments */);
}

TEST(TestdataUtilTest, SectionEscapes) {
  std::string testdata =
      "Line 1\n"
      "\\-- Escaped\n"
      "--\n"
      "\\-- Escaped Again\n"
      "\\--\n"  // Escaped with no following text
      " --\n"   // This does not need escaping.
      "\\\\\n"  // Escaped backlash.
      "Line 2\n"
      "==\n"
      "\\==";

  VerifyGetNextTestdata(testdata, 0, 9,
                        {"Line 1\n"
                         "-- Escaped\n",
                         "-- Escaped Again\n"
                         "--\n"
                         " --\n"
                         "\\\n"
                         "Line 2\n"} /* expected_parts */,
                        {} /* expect no comments */);

  VerifyGetNextTestdata(testdata, 9, 10, {"==\n"} /* expected_parts */,
                        {} /* expect no comments */);
}

// This test has an unnecessary escape at the start of the line that won't be
// preserved in the reassembled output.
TEST(TestdataUtilTest, UnnecessarySectionEscapesAtStartOfLine) {
  std::string testdata =
      "Line 1\n"
      // Unnessary escapes anywhere but start of the line would be preserved.
      " \\--\n"
      "\\Line 2\n"  // Unnecessary escaping at the start of the line.
      "==\n";
  VerifyGetNextTestdata(testdata, 0, 4,
                        {"Line 1\n"
                         " \\--\n"
                         "Line 2\n"} /* expected_parts*/,
                        {} /* expect no comments */,
                        // The reassembled output loses the unnessary escapes.
                        "Line 1\n"
                        " \\--\n"
                        "Line 2\n" /* manual_expected_reassembled */);
}

TEST(TestdataUtilTest, BuildTestFileEntry) {
  EXPECT_EQ("a\nb\n", internal::BuildTestFileEntry({"a\nb\n"} /* parts */,
                                                   {} /* comments */));
}

void TestBreakStringIntoAlternations(
    absl::string_view input,
    std::vector<std::pair<std::string, std::string>> expected) {
  std::vector<std::pair<std::string, std::string>> actual;
  std::vector<std::string> singleton_alternations;
  internal::BreakStringIntoAlternations(input, /*config=*/{}, &actual,
                                        singleton_alternations);

  EXPECT_THAT(singleton_alternations, IsEmpty());
  EXPECT_THAT(actual, ContainerEq(expected));

  actual.clear();
  expected.emplace_back(std::make_pair("", input));

  internal::BreakStringIntoAlternations(
      input, FileBasedTestDriverConfig().set_alternations_enabled(false),
      &actual, singleton_alternations);
  EXPECT_THAT(singleton_alternations, IsEmpty());
  internal::BreakStringIntoAlternations(input, /*config=*/{}, &actual,
                                        singleton_alternations);
}

TEST(TestdataUtilTest, BreakIntoAlternations) {
  std::vector<std::pair<std::string, std::string>> actual;
  std::vector<std::string> singleton_alternations;
  TestBreakStringIntoAlternations("aa{{y||x}}c",
                                  {{"y", "aayc"}, {"", "aac"}, {"x", "aaxc"}});

  TestBreakStringIntoAlternations("aa{{{}|}}c", {{"{}", "aa{}c"}, {"", "aac"}});

  TestBreakStringIntoAlternations("{{a|b}}", {{"a", "a"}, {"b", "b"}});

  TestBreakStringIntoAlternations("{{|a|b}}",
                                  {{"", ""}, {"a", "a"}, {"b", "b"}});

  TestBreakStringIntoAlternations(
      "{{a|b}}{{c|d}}",
      {{"a,c", "ac"}, {"a,d", "ad"}, {"b,c", "bc"}, {"b,d", "bd"}});

  // Unicode
  TestBreakStringIntoAlternations(
      "ɣ{{𝛼|𝛃}}{{c|𝛿}}ζ",
      {{"𝛼,c", "ɣ𝛼cζ"}, {"𝛼,𝛿", "ɣ𝛼𝛿ζ"}, {"𝛃,c", "ɣ𝛃cζ"}, {"𝛃,𝛿", "ɣ𝛃𝛿ζ"}});

  TestBreakStringIntoAlternations("abc", {{"", "abc"}});

  TestBreakStringIntoAlternations("{{a}bc|def{}}",
                                  {{"a}bc", "a}bc"}, {"def{", "def{"}});

  TestBreakStringIntoAlternations("{{a|b}}|", {{"a", "a|"}, {"b", "b|"}});

  // Escaped \ and | inside the alternation groups.
  // This produces an empty alternation at both ends because of the
  // leading and trailing |, and one in the middle between the ||.
  TestBreakStringIntoAlternations(R"(AA{{|ab\|cd||\|x\||\\yy\\|}}BB)",
                                  {{"", "AABB"},
                                   {"ab|cd", "AAab|cdBB"},
                                   {"", "AABB"},
                                   {"|x|", "AA|x|BB"},
                                   {R"(\\yy\\)", R"(AA\\yy\\BB)"},
                                   {"", "AABB"}});

  // Multi-line alternation specifiation.
  TestBreakStringIntoAlternations("\n{{a|b}}\n{{c|d}}\n",
                                  {{"a,c", "\na\nc\n"},
                                   {"a,d", "\na\nd\n"},
                                   {"b,c", "\nb\nc\n"},
                                   {"b,d", "\nb\nd\n"}});

  // Invalid escapes present inside the alternation groups.
  TestBreakStringIntoAlternations(R"({{'a\e'|'\ea'|'\'|'a\aa'}})",
                                  {{R"('a\e')", R"('a\e')"},
                                   {R"('\ea')", R"('\ea')"},
                                   {R"('\')", R"('\')"},
                                   {R"('a\aa')", R"('a\aa')"}});
}

TEST(TestdataUtilTest, BracketNewlineBracketIsNotAnAlternation) {
  // If input text contains {{...<newline>...}} it is ignored as an
  // alternation. It's possible this should be an error, but may be
  // necessary if the underlying language might contain double brackets.
  std::vector<std::pair<std::string, std::string>> actual;
  std::vector<std::string> singleton_alternations;
  TestBreakStringIntoAlternations("{{a|\nb}}", {{"", "{{a|\nb}}"}});
}

TEST(TestdataUtilTest, CatchesSingletonAlternations) {
  std::vector<std::pair<std::string, std::string>> actual;
  std::vector<std::string> singleton_alternations;
  internal::BreakStringIntoAlternations("aa{{b}}c{{}}d{{e}}", /*config=*/{},
                                        &actual, singleton_alternations);

  EXPECT_THAT(singleton_alternations,
              ContainerEq(std::vector<std::string>{"{{b}}", "{{}}", "{{e}}"}));
}

TEST(TestdataUtilTest, ErrorForCommentInTestBody) {
  int line_number = 0;
  std::vector<std::string> parts;
  std::vector<internal::TestCasePartComments> comments;
  FILE_BASED_TEST_DRIVER_EXPECT_OK(internal::GetNextTestCase({"SELECT", "# allowed comment", "1"},
                                      &line_number, &parts, &comments));

  line_number = 0;
  parts.clear();
  comments.clear();
  EXPECT_THAT(
      internal::GetNextTestCase({"SELECT 1", "--", "1", "# wrong comment", "2"},
                                &line_number, &parts, &comments),
      StatusIs(absl::StatusCode::kInvalidArgument));
}

// Callback for RunTestsFromFiles test below. Does some fixed transformations
// based on test input. See implementation for details.
static void RunTestCallback(
    int* num_callbacks, absl::string_view test_case,
    file_based_test_driver::RunTestCaseResult* test_result) {
  LOG(INFO) << "Running test case " << test_case;
  (*num_callbacks)++;
  if (test_case == "line 1\n") {
    test_result->AddTestOutput("Line 2\n");
  } else if (test_case == "line 3\n\nLine 4\n") {
    test_result->AddTestOutput("Line 5\n\nLine 6\n");
  } else if (test_case == "line 7\n") {
    // Nothing.
  } else if (test_case == "line 11\n") {
    test_result->AddTestOutput("Multiple\n");
    test_result->AddTestOutput("Results\n");
  } else if (absl::StartsWith(test_case, "sum")) {
    if (test_case == "sum 3 2 ") {
      EXPECT_EQ("3,2,", test_result->test_alternation());
    } else if (test_case == "sum 3 2 0") {
      EXPECT_EQ("3,2,0", test_result->test_alternation());
    }
    std::vector<std::string> numbers =
        absl::StrSplit(test_case, absl::ByAnyChar(" \n"), absl::SkipEmpty());
    int sum = 0;
    for (int i = 1; i < numbers.size(); ++i) {
      int32_t v;
      CHECK(absl::SimpleAtoi(numbers[i], &v));
      sum += v;
    }
    test_result->AddTestOutput(absl::StrCat("sum ", sum, "\n"));
  } else {
    test_result->AddTestOutput(absl::StrCat("No match for ", test_case));
  }
}

// Callback for RunTestCasesWithModesFromFiles test below. Does the same
// transformations as RunTestCallback(), but using the other framework.
static void RunTestCallbackWithModes(
    int* num_callbacks, absl::string_view test_case,
    file_based_test_driver::RunTestCaseWithModesResult* test_result) {
  LOG(INFO) << "Running test case " << test_case;
  (*num_callbacks)++;
  FILE_BASED_TEST_DRIVER_ASSERT_OK_AND_ASSIGN(TestCaseMode mode, TestCaseMode::Create("DEFAULT_MODE"));
  if (test_case == "line 1\n" || test_case == "line 1copy\n") {
    FILE_BASED_TEST_DRIVER_EXPECT_OK(test_result->mutable_test_case_outputs()->RecordOutput(
        mode, "", "Line 2\n"));
  } else if (test_case == "line 3\n\nLine 4\n") {
    FILE_BASED_TEST_DRIVER_EXPECT_OK(test_result->mutable_test_case_outputs()->RecordOutput(
        mode, "", "Line 5\n\nLine 6\n"));
  } else if (test_case == "line 7\n") {
    // Nothing.
  } else if (test_case == "line 11\n") {
    FILE_BASED_TEST_DRIVER_EXPECT_OK(test_result->mutable_test_case_outputs()->RecordOutput(
        mode, "output1", "Multiple\n"));
    FILE_BASED_TEST_DRIVER_EXPECT_OK(test_result->mutable_test_case_outputs()->RecordOutput(
        mode, "output2", "Results\n"));
  } else if (absl::StartsWith(test_case, "sum")) {
    if (test_case == "sum 3 2 ") {
      EXPECT_EQ("3,2,", test_result->test_alternation());
    } else if (test_case == "sum 3 2 0") {
      EXPECT_EQ("3,2,0", test_result->test_alternation());
    }
    std::vector<std::string> numbers =
        absl::StrSplit(test_case, absl::ByAnyChar(" \n"), absl::SkipEmpty());
    int sum = 0;
    for (int i = 1; i < numbers.size(); ++i) {
      int32_t v;
      CHECK(absl::SimpleAtoi(numbers[i], &v));
      sum += v;
    }
    FILE_BASED_TEST_DRIVER_EXPECT_OK(test_result->mutable_test_case_outputs()->RecordOutput(
        mode, "", absl::StrCat("sum ", sum, "\n")));
  } else {
    FILE_BASED_TEST_DRIVER_EXPECT_OK(test_result->mutable_test_case_outputs()->RecordOutput(
        mode, "", absl::StrCat("No match for ", test_case)));
  }
}

TEST_F(TestdataUtilCallbackTest, RunTestCasesFromFiles) {
  const std::string test_file_contents =
      R"(line 1
--
Line 2
==
line 3

Line 4
--
# Start comment

Line 5

Line 6

# End comment
==
line 7
==
# Case using alterations.
line {{1|3}}
--
ALTERNATION GROUP: 1
--
Line 2
--
ALTERNATION GROUP: 3
--
No match for line 3
==
# Case where alternations have duplicate values.
line {{1|1}}
--
Line 2
==
sum {{1|2}} {{1|2}}
--
ALTERNATION GROUP: 1,1
--
sum 2
--
ALTERNATION GROUPS:
    1,2
    2,1
--
sum 3
--
ALTERNATION GROUP: 2,2
--
sum 4
==
# Case with alternation and multiline results.
line 1{{1|}}
--
ALTERNATION GROUP: 1
--
Multiple
--
Results
--
ALTERNATION GROUP: <empty>
--
Line 2
==
# Case with multiple alternations, several of which have common results.
# Group lists in each alternation group are in their generated order,
# the list of groups is ordered by their first element.
sum {{1|3}} {{2|}} {{|0}}
--
ALTERNATION GROUPS:
    1,2,
    1,2,0
    3,,
    3,,0
--
sum 3
--
ALTERNATION GROUPS:
    1,,
    1,,0
--
sum 1
--
ALTERNATION GROUPS:
    3,2,
    3,2,0
--
sum 5
)";

  internal::RegisteredTempFile test_file("testdata_util_test.test",
                                         test_file_contents);
  int num_callbacks = 0;
  EXPECT_TEST_PASSES(RunTestCasesFromFiles(
      test_file.filename(),
      absl::bind_front(&RunTestCallback, &num_callbacks)));
  EXPECT_EQ(21, num_callbacks);
}

TEST_F(TestdataUtilCallbackTest, SupportConfigDisableAlternations) {
  const std::string test_file_contents =
      R"(Line {{}}{{1}}
--
No match for Line {{}}{{1}}
==
)";

  internal::RegisteredTempFile test_file(
      "support_config_disable_alternations.test", test_file_contents);
  int num_callbacks = 0;
  EXPECT_TEST_PASSES(RunTestCasesFromFiles(
      test_file.filename(), absl::bind_front(&RunTestCallback, &num_callbacks),
      FileBasedTestDriverConfig().set_alternations_enabled(false)));
  EXPECT_EQ(1, num_callbacks);
}

TEST_F(TestdataUtilCallbackTest,
       TestFileRunnerSupportConfigDisableAlternations) {
  const std::string test_file_contents =
      R"(Line {{}}{{1}}
--
No match for Line {{}}{{1}}
==
)";
  internal::RegisteredTempFile test_file(
      "support_config_disable_alternations_via_runner.test",
      test_file_contents);
  int num_callbacks = 0;

  FILE_BASED_TEST_DRIVER_ASSERT_OK_AND_ASSIGN(TestFile file_based_test_file,
                       TestFile::MakeFromFilepath(test_file.filename()));
  std::unique_ptr<TestFileRunner> runner = file_based_test_file.MakeRunner(
      FileBasedTestDriverConfig().set_alternations_enabled(false));

  for (const TestCaseHandle& test_case : file_based_test_file.Tests()) {
    EXPECT_TEST_PASSES(runner->RunTestCase(
        test_case, absl::bind_front(&RunTestCallback, &num_callbacks)));
  }
  EXPECT_EQ(1, num_callbacks);
}

TEST_F(TestdataUtilCallbackTest,
       RunTestCasesFromFilesCatchesSingletonAlternations) {
  const std::string test_file_contents =
      R"(line 1 0123{a|b|c}{{singleton_alt1}}def{{}}  x{{singleton_alt2}}y
--
INVALID_ARGUMENT: Expected at least 2 options in every alternation, but found only one in some. Did you forget to include the empty option? {{singleton_alt1}}, {{}}, {{singleton_alt2}}
)";
  internal::RegisteredTempFile test_file("testdata_util_test.test",
                                         test_file_contents);
  int num_callbacks = 0;
  EXPECT_TEST_PASSES(RunTestCasesFromFiles(
      test_file.filename(),
      absl::bind_front(&RunTestCallback, &num_callbacks)));
  EXPECT_EQ(0, num_callbacks);
}

TEST_F(TestdataUtilCallbackTest, RunTestCasesWithModesFromFiles) {
  const std::string test_file_contents =
      R"(line 1
--
Line 2
==
line 3

Line 4
--
# Start comment

Line 5

Line 6

# End comment
==
line 7
==
# Case using alterations.
line {{1|3}}
--
<{1}>
Line 2
--
<{3}>
No match for line 3
==
# Case where alternations have duplicate outputs.
line {{1|1copy}}
--
Line 2
==
sum {{1|2}} {{1|2}}
--
<{1,1}>
sum 2
--
<{1,2}{2,1}>
sum 3
--
<{2,2}>
sum 4
==
# Case with alternation and multiple outputs per alternation.
line 1{{1|}}
--
<output1{1}>
Multiple
--
<output2{1}>
Results
--
<{EMPTY}>
Line 2
==
# Case with multiple alternations, several of which have common results.
# Group lists in each alternation group are in their generated order,
# the list of groups is ordered by their first element.
sum {{1|3}} {{2|}} {{|0}}
--
<{1,2,}{1,2,0}{3,,}{3,,0}>
sum 3
--
<{1,,}{1,,0}>
sum 1
--
<{3,2,}{3,2,0}>
sum 5
)";

  internal::RegisteredTempFile test_file("testdata_util_test_modes.test",
                                         test_file_contents);

  int num_callbacks = 0;
  EXPECT_TEST_PASSES(RunTestCasesWithModesFromFiles(
      test_file.filename(),
      absl::bind_front(&RunTestCallbackWithModes, &num_callbacks)));
  EXPECT_EQ(21, num_callbacks);
}

TEST_F(TestdataUtilCallbackTest,
       RunTestCasesWithModesFromFilesCatchesSingletonAlternations) {
  const std::string test_file_contents =
      R"(# Case with singleton alternations and multiple outputs per alternation.
line 1{{1|}}{{2}}abc{d|e|f}{{}}pqr{{3}}
--
INVALID_ARGUMENT: Expected at least 2 options in every alternation, but found only one in some. Did you forget to include the empty option? {{2}}, {{}}, {{3}}
)";
  internal::RegisteredTempFile test_file("testdata_util_test.test",
                                         test_file_contents);
  int num_callbacks = 0;
  EXPECT_TEST_PASSES(RunTestCasesWithModesFromFiles(
      test_file.filename(),
      absl::bind_front(&RunTestCallbackWithModes, &num_callbacks)));
  EXPECT_EQ(0, num_callbacks);
}

TEST_F(TestdataUtilCallbackTest, AddBlankLines) {
  const std::string test_file_contents1 =
      R"(line 1
--
Line 2
==
# With no blank lines at the start of the second test case, the test fails.
line 3

Line 4
--
Line 5

Line 6
)";

  const std::string test_file_contents2 =
      R"(line 1
--
Line 2
==


# With two blank lines at the start of the second test case, the test passes.
line 3

Line 4
--
Line 5

Line 6
)";

  internal::RegisteredTempFile test_file1("testdata_util_test1.test",
                                          test_file_contents1);
  internal::RegisteredTempFile test_file2("testdata_util_test2.test",
                                          test_file_contents2);

  FlagSetter flag_setter(
      &FLAGS_file_based_test_driver_insert_leading_blank_lines, 2);

  int num_callbacks = 0;
  EXPECT_TEST_FAILS(RunTestCasesFromFiles(
      test_file1.filename(),
      absl::bind_front(&RunTestCallback, &num_callbacks)));
  EXPECT_TEST_PASSES(RunTestCasesFromFiles(
      test_file2.filename(),
      absl::bind_front(&RunTestCallback, &num_callbacks)));
  EXPECT_TEST_FAILS(RunTestCasesWithModesFromFiles(
      test_file1.filename(),
      absl::bind_front(&RunTestCallbackWithModes, &num_callbacks)));
  EXPECT_TEST_PASSES(RunTestCasesWithModesFromFiles(
      test_file2.filename(),
      absl::bind_front(&RunTestCallbackWithModes, &num_callbacks)));
}

static void RunRegexTestCallback(
    int* num_callbacks, absl::string_view test_case,
    file_based_test_driver::RunTestCaseResult* test_result) {
  LOG(INFO) << "Running test case " << test_case;
  (*num_callbacks)++;
  test_result->AddTestOutput("Result_rep 5\n");
}

// This test has a diff of the form Result_rep 2 in line 3 of the test case.
// The generated value is Result_rep 5. The test fails without regex
// replacement and passes with regex replacement Result_rep \\d+.
// TODO: Test this with WithModes too.
TEST_F(TestdataUtilCallbackTest, ReplaceRegexIgnore) {
  const std::string test_file_contents1 =
      R"(line 1
--
Result_rep 2
==
)";
  internal::RegisteredTempFile test_file1("testdata_util_test1.test",
                                          test_file_contents1);

  int num_callbacks = 0;
  // The test case fails without regex replacement for "Result_rep".
  EXPECT_TEST_FAILS(RunTestCasesFromFiles(
      test_file1.filename(),
      absl::bind_front(&RunRegexTestCallback, &num_callbacks)));

  {
    FlagSetter flag_setter(&FLAGS_file_based_test_driver_ignore_regex, "\\d+");

    // The same test passes after regex replacement for numbers is enabled.
    EXPECT_TEST_PASSES(RunTestCasesFromFiles(
        test_file1.filename(),
        absl::bind_front(&RunRegexTestCallback, &num_callbacks)));
  }
  {
    FlagSetter flag_setter(&FLAGS_file_based_test_driver_ignore_regex, "rep");
    // However replacing "rep" causes the test to still fail.
    EXPECT_TEST_FAILS(RunTestCasesFromFiles(
        test_file1.filename(),
        absl::bind_front(&RunRegexTestCallback, &num_callbacks)));
  }
}

static void EchoCallback(
    int* num_callbacks, absl::string_view test_case,
    file_based_test_driver::RunTestCaseResult* test_result) {
  LOG(INFO) << "Running test case " << test_case;
  (*num_callbacks)++;
  test_result->AddTestOutput(absl::StrCat("Test got input: ", test_case));
}

static void EchoCallbackWithModes(
    int* num_callbacks, absl::string_view test_case,
    file_based_test_driver::RunTestCaseWithModesResult* test_result) {
  LOG(INFO) << "Running test case " << test_case;
  (*num_callbacks)++;
  FILE_BASED_TEST_DRIVER_ASSERT_OK(test_result->mutable_test_case_outputs()->RecordOutput(
      *TestCaseMode::Create("base"), "",
      absl::StrCat("Test got input: ", test_case)));
}

// Test handling of empty tests.
// Tests that have empty input (or only comments) and have no output
// parts are skipped.
// Tests that have output parts are always run, even if empty.
TEST_F(TestdataUtilCallbackTest, EmptyTests) {
  const std::string test_file_contents1 =
      R"(
==
==
# Comment
==
input1
--
Test got input: input1
==
# Comment but no input
--
Test got input: 
==
--
Test got input: 
==
# Comment but no input and no output
==
input2
--
Test got input: input2
==
)";
  const std::string test_file_contents2 =
      R"(
==
==
--
==
==
)";
  internal::RegisteredTempFile test_file1("testdata_util_test1.test",
                                          test_file_contents1);
  internal::RegisteredTempFile test_file2("testdata_util_test2.test",
                                          test_file_contents2);

  int num_callbacks = 0;
  EXPECT_TEST_PASSES(RunTestCasesFromFiles(
      test_file1.filename(), absl::bind_front(&EchoCallback, &num_callbacks)));
  // Four tests have actually run.
  EXPECT_EQ(4, num_callbacks);

  // We had one test with an output, so we ran it, and we fail because the
  // output (empty) didn't match what was expected.
  num_callbacks = 0;
  EXPECT_TEST_FAILS(RunTestCasesFromFiles(
      test_file2.filename(), absl::bind_front(&EchoCallback, &num_callbacks)));
  EXPECT_EQ(1, num_callbacks);
}

TEST_F(TestdataUtilCallbackTest, CaptureDiffOutputs) {
  // All tests passes for this test case.
  const std::string test_file_contents1 =
      R"(
==
==
# Comment
==
input1
--
Test got input: input1
==
# Comment but no input
--
Test got input: 
==
--
Test got input: 
==
# Comment but no input and no output
==
input2
--
Test got input: input2
==
)";

  const std::string test_file_contents2 =
      R"(
==
input1
--
Test got input: input2
==
input3
--
Test got input: input4
==
)";
  internal::RegisteredTempFile test_file1("testdata_util_test1.test",
                                          test_file_contents1);
  internal::RegisteredTempFile test_file2("testdata_util_test2.test",
                                          test_file_contents2);
  std::vector<std::string> diffs;
  auto on_diff_found =
      [&diffs](const file_based_test_driver::ResultDiff& result_diff) {
        diffs.push_back(StringifyResultDiff(result_diff));
      };

  int num_callbacks = 0;
  EXPECT_TEST_PASSES(RunTestCasesFromFiles(
      test_file1.filename(), absl::bind_front(&EchoCallback, &num_callbacks),
      file_based_test_driver::FileBasedTestDriverConfig()
          .set_on_result_diff_found_callback(on_diff_found)));
  // Four tests have actually run.
  EXPECT_EQ(4, num_callbacks);
  EXPECT_THAT(diffs, IsEmpty());
}

static void RunLogIgnoredTestFlagCallback(
    absl::string_view test_case,
    file_based_test_driver::RunTestCaseResult* test_result) {
  static bool ignore_test_output = true;
  test_result->set_ignore_test_output(ignore_test_output);
  ignore_test_output = !ignore_test_output;
}

// Same as RunLogIgnoredTestFlagCallback but using the WithModes output model.
static void RunLogIgnoredTestFlagCallbackWithModes(
    absl::string_view test_case,
    file_based_test_driver::RunTestCaseWithModesResult* test_result) {
  static bool ignore_test_output = true;
  test_result->set_ignore_test_output(ignore_test_output);
  ignore_test_output = !ignore_test_output;
}

// Test FLAGS_file_based_test_driver_log_ignored_test.
TEST_F(TestdataUtilCallbackTest, LogIgnoredTestFlag) {
  // This test manipulates FLAGS_file_based_test_driver_log_ignored_test so
  // certain information is logged into the log file. Please check the log
  // file to verify the expected output.
  const std::string test_file_contents =
      R"(
ignore_this
==
run_this
)";
  internal::RegisteredTempFile test_file("testdata_util_flag_test.test",
                                         test_file_contents);
  FlagSetter setter(&FLAGS_file_based_test_driver_generate_test_output, false);

  // By default, FLAGS_file_based_test_driver_log_ignored_test is true. Log
  // file should look like:
  //
  // Executing tests from file /.../testdata_util_flag_test.test
  // Running test case from /.../testdata_util_flag_test.test, line 1:
  // ignore_this
  // Ignoring test result
  // Running test case from /.../testdata_util_flag_test.test, line 4:
  // run_this
  EXPECT_TEST_PASSES(RunTestCasesFromFiles(test_file.filename(),
                                           &RunLogIgnoredTestFlagCallback));
  EXPECT_TEST_PASSES(RunTestCasesWithModesFromFiles(
      test_file.filename(), &RunLogIgnoredTestFlagCallbackWithModes));
}

using TestFileTest = ::testing::Test;

TEST_F(TestFileTest, MakeFromFilepathWithBadFileReturnsFileNotFound) {
  EXPECT_THAT(TestFile::MakeFromFilepath("bad_file.text"),
              StatusIs(absl::StatusCode::kNotFound));
}

static std::string TestName() {
  // Useful for generating a unique name for an input file.
  return ::testing::UnitTest::GetInstance()->current_test_info()->name();
}

TEST_F(TestFileTest, EmptyFileResultsInOneTest) {
  internal::RegisteredTempFile file(absl::StrCat(TestName(), ".test"), "");
  absl::StatusOr<TestFile> test_file =
      TestFile::MakeFromFilepath(file.filename());
  FILE_BASED_TEST_DRIVER_ASSERT_OK(test_file.status());
  EXPECT_THAT(test_file->Tests(), SizeIs(1));
}

TEST_F(TestFileTest, TestsRepresentsInputFile) {
  internal::RegisteredTempFile file(absl::StrCat(TestName(), ".test"),
                                    R"(
first test
==
second test
--
output part 1
--
output part 2
==
{{alt1|alt2}}
)");
  absl::StatusOr<TestFile> test_file =
      TestFile::MakeFromFilepath(file.filename());
  FILE_BASED_TEST_DRIVER_ASSERT_OK(test_file.status());
  std::vector<TestCaseHandle> tests = test_file->Tests();
  EXPECT_THAT(tests, SizeIs(3));
}

TEST_F(TestFileTest, TestFileRunnerInvokedOncePerAlternation) {
  internal::RegisteredTempFile file(absl::StrCat(TestName(), ".test"),
                                    R"(
first test
--
Test got input: first test
==
with '{{alt1|alt2|}}'
--
ALTERNATION GROUP: alt1
--
Test got input: with 'alt1'
--
ALTERNATION GROUP: alt2
--
Test got input: with 'alt2'
--
ALTERNATION GROUP: <empty>
--
Test got input: with ''
)");
  absl::StatusOr<TestFile> test_file =
      TestFile::MakeFromFilepath(file.filename());
  std::unique_ptr<TestFileRunner> runner = test_file->MakeRunner();
  int num_callbacks = 0;
  for (TestCaseHandle test : test_file->Tests()) {
    runner->RunTestCase(test, absl::bind_front(&EchoCallback, &num_callbacks));
  }
  EXPECT_EQ(num_callbacks, 4);
}

class FileBasedTestDriverTestHelper {
 public:
  static ShardingEnvironment MakeUnshardedEnvironment() {
    return ShardingEnvironment();
  }

  static ShardingEnvironment MakeShardingEnvironment(int this_shard,
                                                     int total_shards) {
    ShardingEnvironment env;
    env.is_sharded_ = true;
    env.this_shard_ = this_shard;
    env.total_shards_ = total_shards;
    return env;
  }

  static int TestIndex(const TestCaseHandle& handle) { return handle.index_; }

  static bool GetSkipBySharding(const TestCaseHandle& handle) {
    return handle.skip_by_sharding_;
  }
};

TEST_F(TestFileTest, TestFileRunnerInvokedOncePerAlternationWithModes) {
  internal::RegisteredTempFile file(absl::StrCat(TestName(), ".test"),
                                    R"(
first test
--
Test got input: first test
==
with '{{alt1|alt2|}}'
--
<{EMPTY}>
Test got input: with ''
--
<{alt1}>
Test got input: with 'alt1'
--
<{alt2}>
Test got input: with 'alt2'
)");
  absl::StatusOr<TestFile> test_file =
      TestFile::MakeFromFilepath(file.filename());
  std::unique_ptr<TestFileRunner> runner = test_file->MakeRunner();
  int num_callbacks = 0;
  for (TestCaseHandle test : test_file->Tests()) {
    runner->RunTestCaseWithModes(
        test, absl::bind_front(&EchoCallbackWithModes, &num_callbacks));
  }
  EXPECT_EQ(num_callbacks, 4);
}

TEST_F(TestFileTest, ShardedTestsWithAlternationsIsUnimplemented) {
  internal::RegisteredTempFile file(absl::StrCat(TestName(), ".test"),
                                    R"(
first test
==
second test
--
output part 1
--
output part 2
==
{{alt1|alt2}}
)");
  FILE_BASED_TEST_DRIVER_ASSERT_OK_AND_ASSIGN(TestFile test_file,
                       TestFile::MakeFromFilepath(file.filename()));
  EXPECT_TRUE(test_file.ContainsAlternations());

  EXPECT_THAT(test_file.ShardedTests(
                  [](const TestCaseInput&) { return false; },
                  FileBasedTestDriverTestHelper::MakeUnshardedEnvironment()),
              StatusIs(absl::StatusCode::kUnimplemented));
}

TEST_F(TestFileTest, ShardedTestsRepresentsInputFileWhenUnsharded) {
  internal::RegisteredTempFile file(absl::StrCat(TestName(), ".test"),
                                    R"(
first test
==
second test
--
output part 1
--
output part 2
==
third test
)");
  FILE_BASED_TEST_DRIVER_ASSERT_OK_AND_ASSIGN(TestFile test_file,
                       TestFile::MakeFromFilepath(file.filename()));
  FILE_BASED_TEST_DRIVER_ASSERT_OK_AND_ASSIGN(
      std::vector<TestCaseHandle> tests,
      test_file.ShardedTests(
          [](const TestCaseInput&) { return false; },
          FileBasedTestDriverTestHelper::MakeUnshardedEnvironment()));

  EXPECT_THAT(tests, SizeIs(3));
}

TEST_F(TestFileTest, ShardedTestsRepresentsInputFileWhenSharded) {
  internal::RegisteredTempFile file(absl::StrCat(TestName(), ".test"),
                                    R"(
first test
==
second test
--
output part 1
--
output part 2
==
third test
)");
  FILE_BASED_TEST_DRIVER_ASSERT_OK_AND_ASSIGN(TestFile test_file,
                       TestFile::MakeFromFilepath(file.filename()));

  const bool run = false;
  const bool skip = true;

  // clang-format off
  const std::vector<int> expected_test_indexes = {
    0, 0, 0, 0,
    1, 1, 1, 1,
    2, 2, 2, 2
  };

  const std::vector<std::vector<bool>> expected_skip_by_sharding = {
      // Shard 1 / 4
      { run,  skip, skip, skip,
        skip, skip, skip, skip,
        skip, skip, skip, skip},
      // Shard 2 / 4 
      { skip, skip, skip, skip,
        skip, run,  skip, skip,
        skip, skip, skip, skip},
      // Shard 3 / 4
      { skip, skip, skip, skip,
        skip, skip, skip, skip,
        skip, skip, run,  skip},
      // Shard 4 / 4 - notice no tests are run on this shard
      { skip, skip, skip, skip,
        skip, skip, skip, skip,
        skip, skip, skip, skip}};
  // clang-format on

  for (int i = 0; i < 4; ++i) {
    FILE_BASED_TEST_DRIVER_ASSERT_OK_AND_ASSIGN(
        std::vector<TestCaseHandle> tests,
        test_file.ShardedTests(
            [](const TestCaseInput&) { return false; },
            FileBasedTestDriverTestHelper::MakeShardingEnvironment(i, 4)));

    // We expect each test_case to exist in each test shard, then googletest
    // will round robin these. So, here, a total of 9 (3 tests, 3 shards).
    ASSERT_THAT(tests, SizeIs(4 * 3));

    for (int t = 0; t < tests.size(); ++t) {
      EXPECT_THAT(FileBasedTestDriverTestHelper::TestIndex(tests[t]),
                  expected_test_indexes[t])
          << "shard = " << i << " t=" << t;
    }
    for (int t = 0; t < tests.size(); ++t) {
      EXPECT_THAT(FileBasedTestDriverTestHelper::GetSkipBySharding(tests[t]),
                  expected_skip_by_sharding[i][t])
          << "shard = " << i << " t=" << t;
    }
  }
}

TEST_F(TestFileTest,
       ShardedTestsRepresentsInputFileWhenShardedAndRespectSideEffects) {
  internal::RegisteredTempFile file(absl::StrCat(TestName(), ".test"),
                                    R"(
first test
==
has side effects
--
output part 1
--
output part 2
==
third test
)");
  FILE_BASED_TEST_DRIVER_ASSERT_OK_AND_ASSIGN(TestFile test_file,
                       TestFile::MakeFromFilepath(file.filename()));

  const bool run = false;
  const bool skip = true;

  // On this test, we mark the second test ("has side effects") as
  // having side effects, meaning it will be run on all shards.

  // clang-format off
  const std::vector<int> expected_test_indexes = {
    0, 0, 0, 0,
    1, 1, 1, 1,
    2, 2, 2, 2
  };
  const std::vector<std::vector<bool>> expected_skip_by_sharding = {
      // Shard 1 / 4
      { run,  skip, skip, skip,
        run,  skip, skip, skip,
        skip, skip, skip, skip},
      // Shard 2 / 4 
      { skip, skip, skip, skip,
        skip, run,  skip, skip,
        skip, skip, skip, skip},
      // Shard 3 / 4
      { skip, skip, skip, skip,
        skip, skip, run,  skip,
        skip, skip, run,  skip},
      // Shard 4 / 4 - notice only the 'side effect' test runs on this shard
      { skip, skip, skip, skip,
        skip, skip, skip, run,
        skip, skip, skip, skip}};
  // clang-format on

  for (int i = 0; i < 4; ++i) {
    FILE_BASED_TEST_DRIVER_ASSERT_OK_AND_ASSIGN(
        std::vector<TestCaseHandle> tests,
        test_file.ShardedTests(
            [](const TestCaseInput& input) {
              return input.text() == "has side effects\n";
            },
            FileBasedTestDriverTestHelper::MakeShardingEnvironment(i, 4)));

    // We expect each test_case to exist in each test shard, then googletest
    // will round robin these. So, here, a total of 12 (3 tests, 4 shards).
    ASSERT_THAT(tests, SizeIs(3 * 4));

    for (int t = 0; t < tests.size(); ++t) {
      EXPECT_THAT(FileBasedTestDriverTestHelper::TestIndex(tests[t]),
                  expected_test_indexes[t])
          << "shard = " << i << " t=" << t;
    }
    for (int t = 0; t < tests.size(); ++t) {
      EXPECT_THAT(FileBasedTestDriverTestHelper::GetSkipBySharding(tests[t]),
                  expected_skip_by_sharding[i][t])
          << "shard = " << i << " t=" << t;
    }
  }
}

}  // namespace file_based_test_driver

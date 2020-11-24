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

#include "file_based_test_driver.h"

#include <stdlib.h>

#include <functional>
#include <string>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/flags/declare.h"
#include "absl/flags/flag.h"
#include "absl/functional/bind_front.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "base/file_util.h"
#include "base/status_matchers.h"

ABSL_DECLARE_FLAG(int32_t, file_based_test_driver_insert_leading_blank_lines);
ABSL_DECLARE_FLAG(bool, file_based_test_driver_generate_test_output);
ABSL_DECLARE_FLAG(bool, file_based_test_driver_individual_tests);
ABSL_DECLARE_FLAG(bool, file_based_test_driver_log_ignored_test);

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
  internal::GetNextTestCase(lines, &line_number, &parts, &comments);
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
      0, 3,
      {"Part 1\n", "Part 2\n"} /* expected_parts */,
      {} /* no expected comments */);

  VerifyGetNextTestdata(
      "Part 1\n"
      "--\n"
      "Part 2\n"
      "--\n"
      "Part 3\n",
      0, 5,
      {"Part 1\n", "Part 2\n", "Part 3\n"} /* expected_parts */,
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
      "\\# not a comment\n"
      " # not a comment\n"
      "# End Comment 1\n"
      "# End Comment 2\n";

  VerifyGetNextTestdata(
      testdata, 0, 9,
      {"# not a \\#comment\n"
       "Line 1\n"
       " foo # not a comment\n"
       "# not a comment\n"
       " # not a comment\n"} /* expected_parts */,
      {{"# Start Comment 1\n# Start Comment 2\n",
        "# End Comment 1\n# End Comment 2\n"}} /* expected_comments */);
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

  VerifyGetNextTestdata(
      testdata, 0, 9,
      {"Line 1\n"
       "-- Escaped\n",
       "-- Escaped Again\n"
       "--\n"
       " --\n"
       "\\\n"
       "Line 2\n"} /* expected_parts */,
      {} /* expect no comments */);

  VerifyGetNextTestdata(
      testdata, 9, 10,
      {"==\n"} /* expected_parts */,
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
  VerifyGetNextTestdata(
      testdata, 0, 4,
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

TEST(TestdataUtilTest, BreakIntoAlternations) {
  std::vector<std::pair<std::string, std::string>> actual;

  internal::BreakStringIntoAlternations("aa{{y||x}}c", &actual);
  EXPECT_THAT(actual,
              ContainerEq(std::vector<std::pair<std::string, std::string>>{
                  {"y", "aayc"}, {"", "aac"}, {"x", "aaxc"}}));

  internal::BreakStringIntoAlternations("aa{{}}c", &actual);
  EXPECT_THAT(actual,
              ContainerEq(std::vector<std::pair<std::string, std::string>>{
                  {"", "aac"}}));

  internal::BreakStringIntoAlternations("{{a|b}}", &actual);
  EXPECT_THAT(actual,
              ContainerEq(std::vector<std::pair<std::string, std::string>>{
                  {"a", "a"}, {"b", "b"}}));

  internal::BreakStringIntoAlternations("{{|a|b}}", &actual);
  EXPECT_THAT(actual,
              ContainerEq(std::vector<std::pair<std::string, std::string>>{
                  {"", ""}, {"a", "a"}, {"b", "b"}}));

  internal::BreakStringIntoAlternations("{{a|b}}{{c|d}}", &actual);
  EXPECT_THAT(actual,
              ContainerEq(std::vector<std::pair<std::string, std::string>>{
                  {"a,c", "ac"}, {"a,d", "ad"}, {"b,c", "bc"}, {"b,d", "bd"}}));

  internal::BreakStringIntoAlternations("abc", &actual);
  EXPECT_THAT(actual,
              ContainerEq(std::vector<std::pair<std::string, std::string>>{
                  {"", "abc"}}));

  internal::BreakStringIntoAlternations("{{a}bc|def{}}", &actual);
  EXPECT_THAT(actual,
              ContainerEq(std::vector<std::pair<std::string, std::string>>{
                  {"a}bc", "a}bc"}, {"def{", "def{"}}));

  internal::BreakStringIntoAlternations("{{a|b}}|", &actual);
  EXPECT_THAT(actual,
              ContainerEq(std::vector<std::pair<std::string, std::string>>{
                  {"a", "a|"}, {"b", "b|"}}));

  // Invalid escapes present inside the alternation groups.
  internal::BreakStringIntoAlternations("{{'a\\e'|'\\ea'|'\\'|'a\\aa'}}",
                                        &actual);
  EXPECT_THAT(actual,
              ContainerEq(std::vector<std::pair<std::string, std::string>>{
                  {"'a\\e'", "'a\\e'"},
                  {"'\\ea'", "'\\ea'"},
                  {"'\\'", "'\\'"},
                  {"'a\\aa'", "'a\\aa'"}}));
}

// Callback for RunTestsFromFiles test below. Does some fixed transformations
// based on test input. See implementation for details.
static void RunTestCallback(
    int* num_callbacks, absl::string_view test_case,
    file_based_test_driver::RunTestCaseResult* test_result) {
  FILE_BASED_TEST_DRIVER_LOG(INFO) << "Running test case " << test_case;
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
      FILE_BASED_TEST_DRIVER_CHECK(absl::SimpleAtoi(numbers[i], &v));
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
  FILE_BASED_TEST_DRIVER_LOG(INFO) << "Running test case " << test_case;
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
      FILE_BASED_TEST_DRIVER_CHECK(absl::SimpleAtoi(numbers[i], &v));
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
  FILE_BASED_TEST_DRIVER_LOG(INFO) << "Running test case " << test_case;
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
  FILE_BASED_TEST_DRIVER_LOG(INFO) << "Running test case " << test_case;
  (*num_callbacks)++;
  test_result->AddTestOutput(absl::StrCat("Test got input: ", test_case));
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

}  // namespace file_based_test_driver

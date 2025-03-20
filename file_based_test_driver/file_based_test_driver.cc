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

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "gtest/gtest.h"
#include "absl/base/nullability.h"
#include "absl/cleanup/cleanup.h"
#include "absl/flags/flag.h"
#include "absl/functional/function_ref.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "absl/synchronization/mutex.h"
#include "absl/types/span.h"
#include "file_based_test_driver/alternations.h"
#include "file_based_test_driver/base/file_util.h"
#include "file_based_test_driver/base/unified_diff.h"
#include "file_based_test_driver/run_test_case_result.h"
#include "file_based_test_driver/test_case_mode.h"
#include "file_based_test_driver/test_case_outputs.h"
#include "re2/re2.h"
#include "file_based_test_driver/base/ret_check.h"
#include "file_based_test_driver/base/status_macros.h"

ABSL_FLAG(int32_t, file_based_test_driver_insert_leading_blank_lines, 0,
          "If this is set to N > 0, then file_based_test_driver will "
          "add leading blank lines to every test case that does not "
          "have N leading blank lines and that is not at the top of the "
          "test file. If a test already has M leading blank lines, then "
          "the number of blank lines added will be N - M.");
ABSL_FLAG(std::string, file_based_test_driver_ignore_regex, "",
          "If this flag is set then all substrings matching this "
          "pattern are replaced with a fixed string on a copy of the "
          "expected output and generated output for diffing.");
ABSL_FLAG(bool, file_based_test_driver_generate_test_output, true,
          "If true, the actual input and output from the test will be "
          "written into the log.  It can be extracted to local files with "
          "fetch-test-results.");
ABSL_FLAG(bool, file_based_test_driver_log_ignored_test, true,
          "If true, the driver will log tests ignored by user defined test "
          "callback. If false, logging will be delayed until the callback "
          "determines if the test is ignored or not.");
ABSL_FLAG(bool, file_based_test_driver_individual_tests, true,
          "If true, each separate diff is evaluated as an EXPECT_EQ which "
          "will in turn cause sponge collected test results to show indidual "
          "diff failures, rather than a simple something went wrong, which "
          "then requires digging through logs manually.");
ABSL_FLAG(int32_t, file_based_test_driver_stack_size_kb, 64,
          "Use this stack size for the thread used to run tests.");

namespace {
template <typename RunTestCaseResultType>
using RunTestCallback =
    absl::FunctionRef<void(absl::string_view, RunTestCaseResultType*)>;
using OnResultDiffFoundCallback = file_based_test_driver::
    FileBasedTestDriverConfig::OnResultDiffFoundCallback;
constexpr size_t kLogBufferSize =
    15000;
constexpr absl::string_view kRootDir =
    "";
}  // namespace

namespace file_based_test_driver {

namespace internal {

static std::string GetWorkspace() {
  const char* workspace_env = getenv("TEST_WORKSPACE");
  if (workspace_env != nullptr) {
    return std::string(workspace_env);
  } else {
    // Backup choice, in the case of running outside of bazel.
    return std::string(kRootDir);
  }
}

std::vector<std::string> SplitTestFileData(absl::string_view file_data) {
  std::vector<std::string> lines = absl::StrSplit(file_data, '\n');
  if (absl::EndsWith(file_data, "\n")) {
    // We treat \n as a line terminator, not a separator.
    lines.resize(lines.size() - 1);
  }
  return lines;
}

void ReadTestFile(absl::string_view filename, std::vector<std::string>* lines) {
  std::string file_data;
  auto status = GetContents(filename, &file_data);
  if (!status.ok()) {
    LOG(FATAL) << "Unable to read: " << filename << ". Failure: " << status;
  }
  *lines = SplitTestFileData(file_data);
}

// This represents one text 'part' of a test case. Each 'part' is separated
// by '--'. There is always exactly 1 'input' part, and 0 or more output
// parts.
struct RawTestCasePart {
  // Starting line of the part. For the input part, this will correspond to
  // the line with '==' (or zero for the first test-case). For output parts
  // it will correspond to the line with '--'.
  int start_line_number;

  // The 'payload' text of the part.  This excludes comments, but _will_
  // include:
  //  - Options:      [x=y]
  //  - Modes:        <XType>[FooMode]
  //  - Alternations: {{a|b|c}}.
  //  - Special text: SAME AS PREVIOUS, ALTERNATION GROUP, etc.
  std::string text;

  // Each part can have an optional starting comment block and an optional
  // ending comment block.
  TestCasePartComments comments;
};

// Representation of the list of alternatives in a single alternation
// group.
struct RawAlternationGroup {
  // Full alternation group text, include braces: e.g. "{{a|b}}"
  std::string raw_text;

  // The list of alternatives. Size must be >= 2. Individual strings
  // may be empty.
  std::vector<std::string> alternatives;
};

// Representation of parsed alternation info of an input part.
//
// The input text is split into N*2 + 1 'fragments' where
// N is the number of alternation groups.
//
// The first fragment is text, the 2nd is the first alternation group
// the 3rd is text again, then the second alternation group, etc.
// finally, the final fragment is text.
// Text fragments may be empty text, but there must be N + 1 such fragments.
//
// Example:
//   ab{{1|2}}{{3|4|5}}ef
//   alternation_groups: [{"1","2"},{"3","4","5"}]
// fragments ["ab", "", "ef"]
//
struct RawAlternationInfo {
  std::vector<RawAlternationGroup> groups;
  std::vector<std::string> input_text_fragments;
};

// Raw representation of each Test case (which are separated by '==')
struct RawTestCase {
  // The filename of the testcase. This is copied into each TestCase
  // for convenience, but will match the RawTestFile containing it.
  std::string filename;

  // The line number of the first line of the TestCase, which
  // will be 0 for the first test case, or correspond to the '=='
  // preceding each test case.
  int start_line_number;

  // The input part. Always the first part in the TestCase.
  RawTestCasePart input_part;

  // The alternation info for the test case. This is parsed
  // from input_part. (Note, input_part will still contain the
  // alternations text, e.g. {{}}).
  RawAlternationInfo alternation_info;

  // Each test run may produce multiple output 'parts'
  // these may be different 'types' of outputs, for
  // instance to represent diffent pieces of metadata about
  // the result of a query. Or results with different
  // modes enabled, or different alternations.
  std::vector<RawTestCasePart> output_parts;
};

// A raw represention of a file based test file
// which is simply a list of test cases.
struct RawTestFile {
  std::string filename;
  std::vector<RawTestCase> test_cases;
};

// Split `str` on '|' characters, allowing for '\' escaping of '|'.
// Other "\x" escape sequences are passed through unescaped.
// Empty tokens are emitted, including one token for empty string input.
static std::vector<std::string> SplitWithEscape(absl::string_view str) {
  std::vector<std::string> result;
  std::string current_token;
  bool escaped = false;

  for (char c : str) {
    if (escaped) {
      // "\|" gets unescaped.  Any other escapes pass through literally.
      if (c != '|') {
        current_token += '\\';
      }
      current_token += c;
      escaped = false;
    } else if (c == '\\') {
      escaped = true;
    } else if (c == '|') {
      result.push_back(current_token);
      current_token = "";  // Clear for the next token.
    } else {
      current_token += c;
    }
  }

  // If there's a stray \ on the end, pass it through.
  if (escaped) {
    current_token += '\\';
  }

  // We get a token for each '|' above, and always get a token at the end of
  // the string for the last segment, even if empty.
  result.push_back(current_token);

  return result;
}

// Regex for finding alternation group. Uses ".*?" so inner matches are
// nongreedy -- we want to get the shortest match, not the longest one.
// Note, alternation groups cannot contain newlines.
static const LazyRE2 kAlternationGroupRegex = {
    R"(((?:.|\n)*?)(\{\{(.*?)\}\}))"};

static RawAlternationInfo ParseRawAlternationInfo(absl::string_view text) {
  absl::string_view input = text;
  RawAlternationInfo output;

  while (true) {
    // The (possibly empty) text before the alternation group.
    absl::string_view prefix;
    // The full alternation text (includes '{{' and '}}')
    absl::string_view full_alternation;
    // Just the 'payload' of the alternation (but still including '|')
    absl::string_view alternation_text;

    // If alternation is not found, append the final fragment and return.
    if (input.empty() || !RE2::Consume(&input, *kAlternationGroupRegex, &prefix,
                                       &full_alternation, &alternation_text)) {
      output.input_text_fragments.emplace_back(input);
      break;
    }
    output.input_text_fragments.emplace_back(prefix);
    output.groups.emplace_back(
        RawAlternationGroup{.raw_text = std::string(full_alternation),
                            .alternatives = SplitWithEscape(alternation_text)});
  }

  return output;
}

// A single fully expanded input, along with the alternations choosen
// to produce it.
struct AlternationExpandedInput {
  std::string MakeAlternationLabel() const {
    // There is (probably accidental) behavior where runs of values that are
    // all empty are all smooshed together, but subsequent empty strings are
    // respected. Example:
    //   [a,b,c] -> "a,b,c"
    //   [,,]    -> ""
    //   [,,a]   -> ",a"
    //   [a,,]   -> "a,,"
    auto it = alternation_values.begin();
    for (; it != alternation_values.end() && it->empty(); ++it) {
    }
    return absl::StrJoin(it, alternation_values.end(), ",");
  }

  std::vector<std::string> alternation_values;
  std::string expanded_input;
};

// Recursive implementation for ExpandAlternations(). Expands one
// alternation from <remaining_groups> and recurses for each alternation value,
// adding the chosen alternation value to expanded_alternations. If there are no
// more alternations in <remaining_groups>, adds (<expanded_alternations>,
// <input_prefix>) to <expanded_alternations>
static void ExpandAlternationsImpl(
    absl::Span<const RawAlternationGroup> remaining_groups,
    absl::Span<const std::string> remaining_fragments,
    absl::string_view input_prefix,
    std::vector<std::string>& alternation_values,
    std::vector<AlternationExpandedInput>& expanded_alternations) {
  CHECK_EQ(remaining_groups.size(), remaining_fragments.size());
  if (remaining_groups.empty()) {
    expanded_alternations.emplace_back(AlternationExpandedInput{
        alternation_values, std::string(input_prefix)});
    return;
  }

  const RawAlternationGroup& current_group = remaining_groups.front();
  // For each alternation value, replace the first alternation group in
  // <input> with that value and recurse to expand the next alternation in
  // <input>.
  for (const std::string& alternation_value : current_group.alternatives) {
    const std::string new_input = absl::StrCat(input_prefix, alternation_value,
                                               remaining_fragments.front());
    // Push on this value temporarily...
    alternation_values.push_back(alternation_value);

    ExpandAlternationsImpl(remaining_groups.subspan(1),
                           remaining_fragments.subspan(1), new_input,
                           alternation_values, expanded_alternations);
    // ... and pop it off for the next iteration.
    alternation_values.pop_back();
  }
}

static void ExpandAlternations(
    const RawAlternationInfo& info,
    std::vector<AlternationExpandedInput>& expanded_alternations,
    std::vector<std::string>& singleton_alternations) {
  CHECK_EQ(info.input_text_fragments.size(), info.groups.size() + 1);

  // Alternations groups that are empty, or contain a single entry
  // (Example: {{a}}) are considered an error since they do nothing, and likely
  // add confusion. So we start by checking for this and returning immediately
  // if any are found.
  for (const RawAlternationGroup& group : info.groups) {
    if (group.alternatives.size() <= 1) {
      singleton_alternations.push_back(group.raw_text);
    }
  }
  if (!singleton_alternations.empty()) {
    return;
  }

  std::vector<std::string> alternation_values;
  ExpandAlternationsImpl(info.groups,
                         absl::MakeSpan(info.input_text_fragments).subspan(1),
                         info.input_text_fragments.front(), alternation_values,
                         expanded_alternations);
}

absl::StatusOr<RawTestCase> ParseNextTestCase(
    absl::string_view filename, const std::vector<std::string>& lines,
    int* line_number /* input and output */) {
  std::string current_part;
  std::string current_comment_start;
  std::string current_comment_end;
  // Will contains the input and possibly output parts.
  std::vector<RawTestCasePart> parts;

  const int test_case_start_line_number = *line_number;
  int part_start_line_number = *line_number;

  for (; *line_number < static_cast<int>(lines.size()); ++*line_number) {
    std::string line = lines[*line_number];

    // Save comments separately.
    if (line.empty() || line[0] == '#') {
      if (current_part.empty()) {
        absl::StrAppend(&current_comment_start, line, "\n");
      } else {
        absl::StrAppend(&current_comment_end, line, "\n");
      }
      continue;
    }

    // "--" is the separator between test case parts.
    // This code first does a very cheap check (StartsWith) before proceeding
    // with the more expensive RE2 check; don't remove the cheap check unless
    // the replacement is also cheap!
    if (absl::StartsWith(line, "--") && RE2::FullMatch(line, "\\-\\-\\s*")) {
      parts.push_back(
          {.start_line_number = part_start_line_number,
           .text = current_part,
           .comments = {current_comment_start, current_comment_end}});
      part_start_line_number = *line_number + 1;
      current_part.clear();
      current_comment_start.clear();
      current_comment_end.clear();
      continue;
    }

    // "==" is the separator between test cases.
    if (absl::StartsWith(line, "==")) {
      ++*line_number;
      break;
    }

    // All of the special cases have been checked, we have an actual test case
    // line.
    if (!current_comment_end.empty()) {
      // We already captured something as a comment in the end of the part.
      // Check if we can retroactively intrepret the comment as part of the
      // test body.
      if (  // The current end comment is a series of empty lines.
          static_cast<size_t>(std::count(current_comment_end.begin(),
                                         current_comment_end.end(), '\n')) ==
              current_comment_end.size() ||
          // We allow comments in the middle of the first part, aka test input.
          parts.empty()) {
        absl::StrAppend(&current_part, current_comment_end);
        current_comment_end.clear();
      } else {
        return absl::InvalidArgumentError(absl::StrCat(
            "Comment \"", current_comment_end,
            "\" is contained within test part \"", current_part, "."));
      }
    }

    // Backslash '\' at the start of the line is used as an escape character for
    // the test framework. Strip them off the actual test content.
    if (absl::StartsWith(line, "\\")) {
      line = line.substr(1);
    }

    absl::StrAppend(&current_part, line, "\n");
  }

  parts.push_back({.start_line_number = part_start_line_number,
                   .text = current_part,
                   .comments = {current_comment_start, current_comment_end}});
  return RawTestCase{.filename = std::string(filename),
                     .start_line_number = test_case_start_line_number,
                     .input_part = parts[0],
                     .alternation_info = ParseRawAlternationInfo(parts[0].text),
                     .output_parts = std::vector<RawTestCasePart>(
                         parts.begin() + 1, parts.end())};
}

absl::StatusOr<RawTestFile> ParseRawTestFile(absl::string_view filename,
                                             absl::string_view contents) {
  std::vector<std::string> lines = SplitTestFileData(contents);
  RawTestFile raw_test_file;
  raw_test_file.filename = std::string(filename);

  int line_number = 0;
  while (line_number < static_cast<int>(lines.size())) {
    FILE_BASED_TEST_DRIVER_ASSIGN_OR_RETURN(raw_test_file.test_cases.emplace_back(),
                     ParseNextTestCase(filename, lines, &line_number));
  }
  return raw_test_file;
}

absl::Status GetNextTestCase(const std::vector<std::string>& lines,
                             int* line_number /* input and output */,
                             std::vector<std::string>* parts,
                             std::vector<TestCasePartComments>* comments) {
  parts->clear();
  comments->clear();
  FILE_BASED_TEST_DRIVER_ASSIGN_OR_RETURN(RawTestCase test_case,
                   ParseNextTestCase(/*filename*/ "", lines, line_number));

  parts->push_back(test_case.input_part.text);
  comments->push_back(test_case.input_part.comments);
  for (const RawTestCasePart& raw_part : test_case.output_parts) {
    parts->push_back(raw_part.text);
    comments->push_back(raw_part.comments);
  }
  return absl::OkStatus();
}

// For each line in 'lines', replaces 'needle' by 'replacement' if it occurs
// at the start of a line. Requires that each line is terminated with an \n.
static void ReplaceAtStartOfLine(absl::string_view needle,
                                 absl::string_view replacement,
                                 std::string* lines) {
  if (lines->empty()) return;
  CHECK(absl::EndsWith(*lines, "\n"));
  std::vector<std::string> split_lines =
      absl::StrSplit(*lines, '\n', absl::AllowEmpty());
  // Disregard the last line, it's not a line, but it's there because 'lines'
  // ends with a newline.
  for (size_t i = 0; i < split_lines.size() - 1; ++i) {
    if (absl::StartsWith(split_lines[i], needle)) {
      split_lines[i].replace(0, needle.size(), replacement);
    }
  }
  *lines = absl::StrJoin(split_lines, "\n");
}

// For the first and the last line in 'lines', replaces 'needle' by
// 'replacement' if it occurs at the start of a line. Requires that each line is
// terminated with an \n.
static void ReplaceAtStartOfFirstAndLastLines(absl::string_view needle,
                                              absl::string_view replacement,
                                              std::string* lines) {
  if (lines->empty()) return;
  CHECK(absl::EndsWith(*lines, "\n"));
  if (absl::StartsWith(*lines, needle)) {
    lines->replace(0, needle.size(), replacement);
  }
  const size_t last_line_start =
      lines->find_last_of('\n', lines->size() - 2) + 1;
  const absl::string_view last_line =
      absl::string_view(*lines).substr(last_line_start);
  if (absl::StartsWith(last_line, needle)) {
    lines->replace(last_line_start, needle.size(), replacement);
  }
}

std::string BuildTestFileEntry(
    const std::vector<std::string>& parts,
    const std::vector<TestCasePartComments>& comments) {
  std::string s;
  for (size_t i = 0; i < parts.size(); ++i) {
    if (i != 0) absl::StrAppend(&s, "--\n");
    std::string part = parts[i];
    ReplaceAtStartOfLine("\\", "\\\\", &part);
    if (i == 0) {
      // In the first part (aka test input), escape only the first and the last
      // # comment. All other lines are allowed to start there with #.
      ReplaceAtStartOfFirstAndLastLines("#", "\\#", &part);
    } else {
      ReplaceAtStartOfLine("#", "\\#", &part);
    }
    ReplaceAtStartOfLine("--", "\\--", &part);
    ReplaceAtStartOfLine("==", "\\==", &part);
    // Empty lines in test output are treated as part of comments when they're
    // at the start or end of the test case. Escape empty lines if they're the
    // first or last line.
    ReplaceAtStartOfFirstAndLastLines("\n", "\\\n", &part);
    if (i < comments.size()) {
      absl::StrAppend(&s, comments[i].start_comment, part,
                      comments[i].end_comment);
    } else {
      absl::StrAppend(&s, part);
    }
  }
  for (size_t i = parts.size(); i < comments.size(); ++i) {
    absl::StrAppend(
        &s,
        comments[i].start_comment.empty() ? ""
                                          : "# COMMENT FROM MISSING PART\n",
        comments[i].start_comment,
        comments[i].end_comment.empty() ? ""
                                        : "# POST-COMMENT FROM MISSING PART\n",
        comments[i].end_comment);
  }
  return s;
}

// Compares the expected output ('expected_string') with the actual output
// ('output_string') and returns true if diff is found. Also appends the
// actual output to 'all_output'.
static bool CompareAndAppendOutput(
    const std::string& expected_string, const std::string& output_string,
    const std::string& test_string, bool matches_requested_same_as_previous,
    absl::string_view filename, int start_line_number,
    const std::vector<TestCasePartComments>& comments, std::string* all_output,
    const OnResultDiffFoundCallback& on_result_diff_found) {
  bool found_diffs = false;

  // If the file_based_test_driver_ignore_regex flag is set, then
  // known regex patterns are replaced with a fixed
  // string on both the left and right of the diff function.
  // This is useful when some known differences have to be ignored.
  // This feature is disabled by default.
  std::string output_string_for_diff = output_string;
  std::string expected_string_for_diff = expected_string;
  if (!absl::GetFlag(FLAGS_file_based_test_driver_ignore_regex).empty()) {
    RE2::GlobalReplace(&output_string_for_diff,
                       absl::GetFlag(FLAGS_file_based_test_driver_ignore_regex),
                       "");
    RE2::GlobalReplace(&expected_string_for_diff,
                       absl::GetFlag(FLAGS_file_based_test_driver_ignore_regex),
                       "");
  }

  if (expected_string_for_diff != expected_string) {
    LOG(WARNING) << "Expected output is modified for diff because of "
                 << "file_based_test_driver_ignore_regex flag";
  }

  if (output_string_for_diff != output_string) {
    LOG(WARNING) << "Generated Output from test case is modified for diff "
                 << "because of file_based_test_driver_ignore_regex flag";
  }

  // Compare output.
  if (expected_string_for_diff != output_string_for_diff) {
    std::vector<std::string> parts =
        absl::StrSplit(filename, absl::StrCat("/", GetWorkspace(), "/"));
    std::string relpath = parts[parts.size() - 1];
    std::string diff = file_based_test_driver::UnifiedDiff(
        expected_string_for_diff, output_string_for_diff,
        absl::StrCat("expected/", relpath), absl::StrCat("actual/", relpath),
        file_based_test_driver::UnifiedDiffOptions().set_context_size(5));
    on_result_diff_found(ResultDiff{
        .unified_diff = diff,
        .expected = expected_string,
        .actual = output_string,
        .file_path = relpath,
        .start_line_number = start_line_number,
    });

    found_diffs = true;
    if (absl::GetFlag(FLAGS_file_based_test_driver_individual_tests)) {

      // EXPECT_EQ does its own diff, but escapes carriage returns.
      ADD_FAILURE_AT(relpath.c_str(), start_line_number + 1)
          << "\n\n******************* BEGIN TEST DIFF ********************"
          << "\nFailure in " << filename << ", line " << start_line_number + 1
          << ":\n"
          << "\n=================== DIFF ===============================\n"
          << diff;
      // Separate log message to avoid truncation in the case of long
      // output.
      ADD_FAILURE_AT(relpath.c_str(), start_line_number + 1)
          << "=================== EXPECTED ===========================\n"
          << expected_string
          << "=================== ACTUAL =============================\n"
          << output_string
          << "******************* END TEST DIFF **********************\n\n";
    } else {
      LOG(WARNING)
          << "\n\n******************* BEGIN TEST DIFF ********************"
          << "\nFailure in " << filename << ", line " << start_line_number + 1
          << ":\n"
          << "=================== EXPECTED ===========================\n"
          << expected_string
          << "=================== ACTUAL =============================\n"
          << output_string;

      // Separate log message to avoid truncation in the case of long
      // output.
      LOG(WARNING)
          << "\n=================== DIFF ===============================\n"
          << diff
          << "******************* END TEST DIFF **********************\n\n";
    }
  }

  // Add to all_output.
  if (!all_output->empty()) absl::StrAppend(all_output, "==\n");
  if (matches_requested_same_as_previous) {
    absl::StrAppend(all_output,
                    internal::BuildTestFileEntry(
                        {test_string, "[SAME AS PREVIOUS]\n"}, comments));
  } else {
    absl::StrAppend(all_output, output_string);
  }

  return found_diffs;
}

// Templated class to map a RunTestCaseResult instance into the appropriate
// alternation collection implementation.
template <class RunTestCaseResultType>
struct AlternationSetType;

template <>
struct AlternationSetType<RunTestCaseResult> {
  using Type = AlternationSet;
};

template <>
struct AlternationSetType<RunTestCaseWithModesResult> {
  using Type = AlternationSetWithModes;
};

absl::Status FailTestWithMessage(RunTestCaseResult* result,
                                 const absl::string_view message) {
  std::vector<std::string>* outputs = result->mutable_test_outputs();
  outputs->emplace_back(result->parts()[0]);
  outputs->emplace_back(message);
  return absl::OkStatus();
}

absl::Status FailTestWithMessage(RunTestCaseWithModesResult* result,
                                 const absl::string_view message) {
  // Note: the mode is a dummy here, as it doesn't get a chance to appear in
  // the output, since the output is failing the whole test, not just one
  // alternation.
  return result->mutable_test_case_outputs()->RecordOutput(
      TestCaseMode::Create("?").value(), "", message);
}

template <class RunTestCaseResultType>
absl::Status RunAlternations(
    RunTestCaseResultType* result,
    RunTestCallback<RunTestCaseResultType> run_test_case,
    const FileBasedTestDriverConfig& config) {
  FILE_BASED_TEST_DRIVER_RET_CHECK(result->IsEmpty());

  // Calculate expanded values for the cross product of all alternations
  // of the form {{a|b}} in parts_[0].

  // A vector of alternation group values and the test case strings
  // expanded with those values.
  std::vector<std::pair<std::string, std::string>>
      alternation_values_and_expanded_inputs;

  std::vector<std::string> singleton_alternations;
  internal::BreakStringIntoAlternations(result->parts()[0], config,
                                        &alternation_values_and_expanded_inputs,
                                        singleton_alternations);

  if (!singleton_alternations.empty()) {
    result->set_ignore_test_output(false);

    const std::string message = absl::StrCat(
        "INVALID_ARGUMENT: Expected at least 2 options in every alternation, "
        "but found only one in some. Did you forget to include the empty "
        "option? ",
        absl::StrJoin(singleton_alternations, ", "));

    // The test fails, but the driver/runner itself is fine. Return OK so that
    // we can report the singletons with all the details.
    return FailTestWithMessage(result, message);
  }

  // Default to ignoring results. Disable ignoring if any alternation groups not
  // ignored.
  result->set_ignore_test_output(true);

  typename AlternationSetType<RunTestCaseResultType>::Type alternation_set;
  for (size_t alternation_idx = 0;
       alternation_idx < alternation_values_and_expanded_inputs.size();
       ++alternation_idx) {
    const std::string& test_alternation =
        alternation_values_and_expanded_inputs[alternation_idx].first;
    const std::string& test_case =
        alternation_values_and_expanded_inputs[alternation_idx].second;
    if (alternation_values_and_expanded_inputs.size() != 1) {
      LOG(INFO) << "Running alternation " << test_alternation;
    }
    RunTestCaseResultType sub_test_result;
    // Pass file name, line number, and parts of the test along with
    // test_result.
    sub_test_result.set_filename(result->filename());
    sub_test_result.set_line(result->line());
    sub_test_result.set_parts(result->parts());
    sub_test_result.set_test_alternation(test_alternation);
    run_test_case(test_case, &sub_test_result);
    if (!sub_test_result.ignore_test_output()) {
      result->set_ignore_test_output(false);
      FILE_BASED_TEST_DRIVER_RETURN_IF_ERROR(
          alternation_set.Record(test_alternation, sub_test_result));
    }
  }

  FILE_BASED_TEST_DRIVER_RETURN_IF_ERROR(alternation_set.Finish(result));

  return absl::OkStatus();
}

void BreakStringIntoAlternations(
    absl::string_view input, const FileBasedTestDriverConfig& config,
    std::vector<std::pair<std::string, std::string>>*
        labels_and_expanded_inputs,
    std::vector<std::string>& singleton_alternations) {
  if (!config.alternations_enabled()) {
    labels_and_expanded_inputs->emplace_back("", input);
    return;
  }
  labels_and_expanded_inputs->clear();
  std::vector<AlternationExpandedInput> alternation_expanded_inputs;
  ExpandAlternations(ParseRawAlternationInfo(input),
                     alternation_expanded_inputs, singleton_alternations);
  for (const AlternationExpandedInput& inputs : alternation_expanded_inputs) {
    labels_and_expanded_inputs->emplace_back(inputs.MakeAlternationLabel(),
                                             inputs.expanded_input);
  }
}

}  // namespace internal

// Writes <text> with a associated path 'file_path' to the INFO log.
// The output starts with a line '****<test_output_prefix>_BEGIN****<file_path>'
// and ends with a line '****<test_output_prefix>_END****'.
// See comment on LogExtractableText in file_based_test_driver.h for details.
static void LogExtractableText(absl::string_view file_path,
                               absl::string_view text,
                               absl::string_view test_output_prefix) {
  // We have to split it up because there is a limit on the size of log
  // messages. The output will be reassembled into a single file by
  // extract_test_output.py.
  std::deque<absl::string_view> output_lines = absl::StrSplit(text, '\n');
  // 'test_output' ends in '\n', this adds an extra line at the end that we
  // don't want, remove it.
  CHECK_EQ(output_lines.back(), "");
  output_lines.pop_back();
  bool first_output_block = true;
  while (!output_lines.empty()) {
    std::string this_output;
    while (!output_lines.empty()) {
      if (this_output.size() + output_lines[0].size() + file_path.size() +
              500 /* padding */
          > kLogBufferSize) {
        if (this_output.empty()) {
          const int prefix_len =
              kLogBufferSize - file_path.size() - 500 /* padding */;
          absl::StrAppend(&this_output, output_lines[0].substr(0, prefix_len),
                          "\n***MERGE_TOO_LONG_LINE***\n");
          output_lines[0] = output_lines[0].substr(prefix_len);
        }
        break;
      }
      absl::StrAppend(&this_output, output_lines[0], "\n");
      output_lines.erase(output_lines.begin());
    }
    LOG(INFO) << "\n"
              << "****" << test_output_prefix << "_BEGIN**** "
              << (first_output_block ? "NEW_TEST_RUN " : "") << file_path
              << "\n"
              << this_output << "****" << test_output_prefix << "_END****\n";
    first_output_block = false;
  }
}

void LogExtractableText(absl::string_view file_path, absl::string_view text) {
  LogExtractableText(file_path, text, "TEST_OUTPUT");
}

namespace {

// Class that keeps track of the output of all the test cases that have run
// so far.
class AbstractRunTestCaseOutput {
 public:
  virtual ~AbstractRunTestCaseOutput() {}

  // Returns the string that contains the output of all the test cases.
  virtual std::string* GetAllOutput() = 0;

  // Appends the test output to the log file at 'file_path'.
  virtual void AddOutputToLog(const std::string& file_path) = 0;
};

}  // namespace

namespace internal {
// Implementation of AbstractRunTestCaseOutput for regular test cases with
// simple string outputs.
class RunTestCaseOutput : public AbstractRunTestCaseOutput {
 public:
  RunTestCaseOutput() {}
  ~RunTestCaseOutput() override {}
  RunTestCaseOutput(const RunTestCaseOutput&) = delete;
  RunTestCaseOutput& operator=(const RunTestCaseOutput&) = delete;

  std::string* GetAllOutput() override { return &all_output_; }

  void AddOutputToLog(const std::string& file_path) override {
    LogExtractableText(file_path, all_output_);
  }

  const std::vector<std::string>& prev_output() const { return prev_output_; }

  void set_prev_output(const std::vector<std::string>& prev_output) {
    prev_output_ = prev_output;
  }

 private:
  std::string all_output_;
  std::vector<std::string> prev_output_;
};

// Implementation of AbstractRunTestCaseOutput for test cases with mode
// specific output.
class RunTestCaseWithModesOutput : public AbstractRunTestCaseOutput {
 public:
  RunTestCaseWithModesOutput() {}
  ~RunTestCaseWithModesOutput() override {}
  RunTestCaseWithModesOutput(const RunTestCaseWithModesOutput&) = delete;
  RunTestCaseWithModesOutput& operator=(const RunTestCaseWithModesOutput&) =
      delete;
  std::string* GetAllOutput() override { return &all_merged_output_; }

  void AddOutputToLog(const std::string& file_path) override {
    LogExtractableText(file_path, all_merged_output_, "MERGED_TEST_OUTPUT");
    LogExtractableText(file_path, all_actual_output_);
  }

  const std::string& all_actual_output() const { return all_actual_output_; }

  std::string* mutable_all_actual_output() { return &all_actual_output_; }

  const TestCaseOutputs* prev_expected_outputs() const {
    return prev_expected_outputs_.get();
  }

  const TestCaseOutputs* prev_merged_outputs() const {
    return prev_merged_outputs_.get();
  }

  void ResetOutputs(const TestCaseOutputs& prev_expected_outputs,
                    const TestCaseOutputs& prev_merged_outputs) {
    prev_expected_outputs_ =
        std::make_unique<TestCaseOutputs>(prev_expected_outputs);
    prev_merged_outputs_ =
        std::make_unique<TestCaseOutputs>(prev_merged_outputs);
  }

 private:
  std::string all_actual_output_;
  std::string all_merged_output_;
  std::unique_ptr<TestCaseOutputs> prev_expected_outputs_;
  std::unique_ptr<TestCaseOutputs> prev_merged_outputs_;
};

}  // namespace internal

namespace {

using internal::RunTestCaseOutput;
using internal::RunTestCaseWithModesOutput;

template <typename RunTestCaseResultType>
bool RunTestCasesFromOneFile(
    absl::string_view filename,
    RunTestCallback<RunTestCaseResultType> run_test_case,
    const FileBasedTestDriverConfig& config) {
  bool all_passed = true;
  absl::StatusOr<TestFile> test_file = TestFile::MakeFromFilepath(filename);
  CHECK_OK(test_file);

  LOG(INFO) << "Executing tests from file " << filename;
  std::unique_ptr<TestFileRunner> runner = test_file->MakeRunner(config);
  for (const TestCaseHandle& test_case : runner->test_file().Tests()) {
    if constexpr (std::is_same_v<RunTestCaseResultType, RunTestCaseResult>) {
      all_passed &= runner->RunTestCase(test_case, run_test_case);
    } else {
      all_passed &= runner->RunTestCaseWithModes(test_case, run_test_case);
    }
  }
  return all_passed;
}

// Template function that implements RunTestCasesFromFiles and
// RunTestCasesWithModesFromFiles. See comments in file_based_test_driver.h for
// more details.
template <typename RunTestCaseResultType>
bool RunTestCasesFromFiles(absl::string_view filespec,
                           RunTestCallback<RunTestCaseResultType> run_test_case,
                           const FileBasedTestDriverConfig& config) {
  bool no_diffs = true;
  std::vector<std::string> test_files;
  CHECK_OK(internal::Match(filespec, &test_files)) << "Filespec " << filespec;
  CHECK_GT(test_files.size(), size_t{0}) << "Filespec " << filespec;
  for (const std::string& filename : test_files) {
    no_diffs &= RunTestCasesFromOneFile<RunTestCaseResultType>(
        filename, run_test_case, config);
  }
  return no_diffs;
}

// If FLAGS_file_based_test_driver_insert_leading_blank_lines is set, adds the
// required number of blank lines to the start of 'comments' if they are not
// already present. 'filename' and 'start_line_number' should be the file name
// and line number of the test case. Blank lines are not added for test cases
// that start at line 0.
static bool AddBlankLines(
    absl::string_view filename, int start_line_number,
    std::vector<internal::TestCasePartComments>* comments) {
  bool added_lines = false;
  if (absl::GetFlag(FLAGS_file_based_test_driver_insert_leading_blank_lines) >
          0 &&
      start_line_number > 0) {
    // We're not in the first test case, and the flag says we need to make
    // sure that every test case starts with a number of blank lines
    // before the test case's comments. Add them if they're not there.
    CHECK(!comments->empty());  // The test case is always present.
    while (!absl::StartsWith(
        (*comments)[0].start_comment,
        std::string(
            absl::GetFlag(
                FLAGS_file_based_test_driver_insert_leading_blank_lines),
            '\n'))) {
      (*comments)[0].start_comment =
          absl::StrCat("\n", (*comments)[0].start_comment);
      // Make sure the diffs cause a test failure, so that users can update
      // their goldens with the added empty lines.
      if (absl::GetFlag(FLAGS_file_based_test_driver_individual_tests)) {
        ADD_FAILURE() << "Test without leading blank line in " << filename
                      << ", line " << start_line_number + 1;
      } else {
        LOG(INFO) << "Test without leading blank line in " << filename
                  << ", line " << start_line_number + 1;
      }
      added_lines = true;
    }
  }
  return added_lines;
}

// Runs one test case and compares its output with the expected output.
// Returns true if diff is found.
// 'test_case': The single test to be run.
// 'run_test_case': the callback to run the test, may be invoked multiple times
//                  due to alternations. May not be run at all if empty.
// 'all_output': class that keeps track of the output for all test cases.
bool RunOneTestCase(
    const internal::RawTestCase& test_case,
    absl::FunctionRef<void(absl::string_view test_input,
                           RunTestCaseResult* test_case_result /*out*/)>
        run_test_case,
    const FileBasedTestDriverConfig& config, RunTestCaseOutput* all_output) {
  CHECK(all_output != nullptr);
  const int start_line_number = test_case.start_line_number;
  const absl::string_view filename = test_case.filename;

  std::vector<std::string> raw_parts;
  std::vector<internal::TestCasePartComments> comments;
  raw_parts.push_back(test_case.input_part.text);
  comments.push_back(test_case.input_part.comments);
  for (const internal::RawTestCasePart& part : test_case.output_parts) {
    raw_parts.push_back(part.text);
    comments.push_back(part.comments);
  }

  // Run test.
  const std::string test_case_log =
      absl::StrCat("test case from ", filename, ", line ",
                   start_line_number + 1, ":\n", test_case.input_part.text);

  bool ignore_test_output = false;
  bool matches_requested_same_as_previous = false;

  std::vector<std::string> output;

  if (test_case.input_part.text.empty() && test_case.output_parts.empty()) {
    // Skip empty test cases if there's no expected output.
    // If there is an expected output, then we'll try to run the test
    // with an empty input.
    LOG(INFO) << "Skipping empty test case from " << filename << ", line "
              << start_line_number + 1 << ".";
    output = {""};
  } else {
    // What will be logged in the log file:
    //
    //       \   log     |  T                      |  F
    // ignore \  ignored |                         |
    // test    \ test    |                         |
    // output            |                         |
    // ------------------+-------------------------+------------------------
    //    T              | Running test ...        | <test_execution_if_any>
    //                   | <test_execution_if_any> |
    //                   | Ignoring test result    |
    // ------------------+-------------------------+------------------------
    //    F              | Running test ...        | <test_execution_if_any>
    //                   | <test_execution_if_any> | Executed test ...
    //
    // See also LogIgnoredTestFlag in file_based_test_driver_test.cc.
    if (absl::GetFlag(FLAGS_file_based_test_driver_log_ignored_test)) {
      LOG(INFO) << "Running " << test_case_log;
    }
    // Otherwise, we delay the logging until we know if the test is ignored.

    RunTestCaseResult test_result;
    // Pass file name, line number, and parts of the test along with
    // test_result.
    test_result.set_filename(std::string(filename));
    test_result.set_line(start_line_number + 1);
    test_result.set_parts(raw_parts);
    CHECK_OK(internal::RunAlternations(&test_result, run_test_case, config));
    output = test_result.test_outputs();
    ignore_test_output = test_result.ignore_test_output();
  }

  // Ensure all nonempty parts end in \n.
  for (std::string& output_part : output) {
    if (!output_part.empty() && !absl::EndsWith(output_part, "\n")) {
      absl::StrAppend(&output_part, "\n");
    }
  }

  // If the callback set the 'ignore_test_output' boolean to true,
  // ignore this test's output. We still pretend the output was correct,
  // so that the generated output file has all the skipped parts as well.
  // This helps in diffing the output with the original.
  const std::string kSameAsPrevious = "[SAME AS PREVIOUS]\n";
  bool update_prev_output = true;
  if (ignore_test_output) {
    if (absl::GetFlag(FLAGS_file_based_test_driver_log_ignored_test)) {
      LOG(INFO) << "Ignoring test result";
    }
    output = raw_parts;
    if (raw_parts.size() == 2 && raw_parts[1] == kSameAsPrevious) {
      // Do not update 'prev_output' if the test output says
      // "[SAME AS PREVIOUS]" and the test is ignored.
      update_prev_output = false;
    }
  } else {
    // Special feature: if the test output says "[SAME AS PREVIOUS]", then
    // replace the expected output with the output from the previous test
    // run. Enabling same-output checking should be an explicit thing, used
    // only when two tests should theoretically always yield the same
    // results.
    if (raw_parts.size() == 2 && raw_parts[1] == kSameAsPrevious &&
        !all_output->prev_output().empty()) {
      raw_parts.resize(1); /* keep input */
      raw_parts.insert(raw_parts.end(),
                       all_output->prev_output().begin() + 1 /* skip input */,
                       all_output->prev_output().end());
      if (output == raw_parts) {
        matches_requested_same_as_previous = true;
      }
    }

    if (!absl::GetFlag(FLAGS_file_based_test_driver_log_ignored_test)) {
      LOG(INFO) << "Executed " << test_case_log;
    }
  }

  if (update_prev_output) {
    all_output->set_prev_output(output);
  }

  const bool added_blank_lines =
      AddBlankLines(filename, start_line_number, &comments);

  const std::string output_string =
      internal::BuildTestFileEntry(output, comments);
  const std::string expected_string =
      internal::BuildTestFileEntry(raw_parts, comments);

  return internal::CompareAndAppendOutput(
             expected_string, output_string, raw_parts[0],
             matches_requested_same_as_previous, filename, start_line_number,
             comments, all_output->GetAllOutput(),
             config.on_result_diff_found()) ||
         added_blank_lines;
}

// Runs one test case and compares its output with the expected output.
// Returns true if diff is found.
// 'test_case': The single test to be run.
// 'run_test_case': the callback to run the test, may be invoked multiple times
//                  due to alternations. May not be run at all if empty.
// 'all_output': class that keeps track of the output for all test cases.
bool RunOneTestCase(const internal::RawTestCase& test_case,
                    RunTestCallback<RunTestCaseWithModesResult> run_test_case,
                    const FileBasedTestDriverConfig& config,
                    RunTestCaseWithModesOutput* all_output) {
  CHECK(all_output != nullptr);
  const int start_line_number = test_case.start_line_number;
  const absl::string_view filename = test_case.filename;

  std::vector<std::string> raw_parts;
  std::vector<internal::TestCasePartComments> comments;
  raw_parts.push_back(test_case.input_part.text);
  comments.push_back(test_case.input_part.comments);
  for (const internal::RawTestCasePart& part : test_case.output_parts) {
    raw_parts.push_back(part.text);
    comments.push_back(part.comments);
  }

  const std::string test_case_log =
      absl::StrCat("test case from ", filename, ", line ",
                   start_line_number + 1, ":\n", test_case.input_part.text);
  bool ignore_test_output = false;
  bool matches_requested_same_as_previous = false;
  const std::string kSameAsPrevious = "[SAME AS PREVIOUS]\n";
  TestCaseOutputs expected_outputs;
  TestCaseOutputs merged_outputs;
  RunTestCaseWithModesResult test_result;

  if (test_case.input_part.text.empty() && test_case.output_parts.empty()) {
    // Skip empty test cases if there's no expected output.
    // If there is an expected output, then we'll try to run the test
    // with an empty input.
    LOG(INFO) << "Skipping empty test case from " << filename << ", line "
              << start_line_number + 1 << ".";
  } else {
    // What will be logged in the log file:
    //
    //       \   log     |  T                      |  F
    // ignore \  ignored |                         |
    // test    \ test    |                         |
    // output            |                         |
    // ------------------+-------------------------+------------------------
    //    T              | Running test ...        | <test_execution_if_any>
    //                   | <test_execution_if_any> |
    //                   | Ignoring test result    |
    // ------------------+-------------------------+------------------------
    //    F              | Running test ...        | <test_execution_if_any>
    //                   | <test_execution_if_any> | Executed test ...
    //
    // See also LogIgnoredTestFlag in file_based_test_driver_test.cc.
    if (absl::GetFlag(FLAGS_file_based_test_driver_log_ignored_test)) {
      LOG(INFO) << "Running " << test_case_log;
    }
    // Otherwise, we delay the logging until we know if the test is ignored.

    if (raw_parts.size() == 2 && raw_parts[1] == kSameAsPrevious &&
        all_output->prev_expected_outputs() != nullptr) {
      expected_outputs = *all_output->prev_expected_outputs();
    } else {
      const std::vector<std::string> outputs(raw_parts.begin() + 1,
                                             raw_parts.end());
      CHECK_OK(expected_outputs.ParseFrom(outputs)) << test_case_log;
    }

    // Pass file name, line number, and parts of the test along with
    // test_result.
    test_result.set_filename(std::string(filename));
    test_result.set_line(start_line_number + 1);
    test_result.set_parts(raw_parts);
    CHECK_OK(internal::RunAlternations(&test_result, run_test_case, config))
        << test_case_log;
    if (test_result.ignore_test_output()) {
      ignore_test_output = true;
    } else {
      CHECK_OK(TestCaseOutputs::MergeOutputs(
          expected_outputs, {test_result.test_case_outputs()}, &merged_outputs))
          << test_case_log;
    }
  }

  // If the callback set the 'ignore_test_output' boolean to true,
  // ignore this test's output. We still pretend the output was correct,
  // so that the generated output file has all the skipped parts as well.
  // This helps in diffing the output with the original.
  bool update_prev_output = true;
  if (ignore_test_output) {
    if (absl::GetFlag(FLAGS_file_based_test_driver_log_ignored_test)) {
      LOG(INFO) << "Ignoring test result";
    }
    merged_outputs = expected_outputs;
    if (raw_parts.size() == 2 && raw_parts[1] == kSameAsPrevious) {
      // Do not update 'prev_output' if the test output says
      // "[SAME AS PREVIOUS]" and the test is ignored.
      update_prev_output = false;
    }
  } else {
    // Special feature: if the test output says "[SAME AS PREVIOUS]", then
    // replace the expected output with the output from the previous test
    // run. Enabling same-output checking should be an explicit thing, used
    // only when two tests should theoretically always yield the same
    // results.
    if (raw_parts.size() == 2 && raw_parts[1] == kSameAsPrevious &&
        all_output->prev_merged_outputs() != nullptr) {
      if (*all_output->prev_merged_outputs() == merged_outputs) {
        matches_requested_same_as_previous = true;
      }
    }

    if (!absl::GetFlag(FLAGS_file_based_test_driver_log_ignored_test)) {
      LOG(INFO) << "Executed " << test_case_log;
    }
  }

  if (update_prev_output) {
    all_output->ResetOutputs(expected_outputs, merged_outputs);
  }

  std::vector<std::string> expected_outputs_parts;
  expected_outputs_parts.push_back(raw_parts[0]);
  CHECK_OK(expected_outputs.GetCombinedOutputs(
      false /* include_possible_modes */, &expected_outputs_parts))
      << test_case_log;

  std::vector<std::string> merged_outputs_parts;
  merged_outputs_parts.push_back(raw_parts[0]);
  CHECK_OK(merged_outputs.GetCombinedOutputs(false /* include_possible_modes */,
                                             &merged_outputs_parts))
      << test_case_log;

  std::vector<std::string> actual_outputs_parts;
  actual_outputs_parts.push_back(raw_parts[0]);
  CHECK_OK(test_result.test_case_outputs().GetCombinedOutputs(
      true /* include_possible_modes */, &actual_outputs_parts))
      << test_case_log;

  const bool added_blank_lines =
      AddBlankLines(filename, start_line_number, &comments);

  const std::string output_string =
      internal::BuildTestFileEntry(merged_outputs_parts, comments);
  const std::string expected_string =
      internal::BuildTestFileEntry(expected_outputs_parts, comments);
  const std::string actual_output_string =
      internal::BuildTestFileEntry(actual_outputs_parts, comments);

  bool found_diffs =
      internal::CompareAndAppendOutput(
          expected_string, output_string, raw_parts[0],
          matches_requested_same_as_previous, filename, start_line_number,
          comments, all_output->GetAllOutput(),
          config.on_result_diff_found()) ||
      added_blank_lines;

  if (!all_output->all_actual_output().empty()) {
    absl::StrAppend(all_output->mutable_all_actual_output(), "==\n");
  }
  absl::StrAppend(all_output->mutable_all_actual_output(),
                  actual_output_string);
  return found_diffs;
}

}  // namespace

TestFile::TestFile(const TestFile& v)
    : raw_test_file_(
          std::make_unique<internal::RawTestFile>(v.raw_test_file())) {}

TestFile& TestFile::operator=(const TestFile& v) {
  raw_test_file_ = std::make_unique<internal::RawTestFile>(v.raw_test_file());
  return *this;
}

TestFile::TestFile(TestFile&& v) = default;
TestFile& TestFile::operator=(TestFile&& v) = default;

TestFile::~TestFile() = default;

absl::StatusOr<TestFile> TestFile::MakeFromFilepath(
    absl::string_view file_path) {
  std::string file_data;
  FILE_BASED_TEST_DRIVER_RETURN_IF_ERROR(internal::GetContents(file_path, &file_data));

  FILE_BASED_TEST_DRIVER_ASSIGN_OR_RETURN(internal::RawTestFile test_file,
                   internal::ParseRawTestFile(file_path, file_data));
  TestFile out;
  out.raw_test_file_ =
      std::make_unique<internal::RawTestFile>(std::move(test_file));
  return out;
}

std::vector<TestCaseHandle> TestFile::Tests() const {
  std::string file_label = "file_based_test_driver";

  std::vector<std::string> parts = absl::StrSplit(filename(), '/');
  if (!parts.empty()) {
    absl::string_view final_part = parts.back();
    file_label = absl::StrCat(absl::StripSuffix(final_part, ".test"), "_");
  }
  std::vector<TestCaseHandle> tests;
  int index = 0;
  for (const internal::RawTestCase& raw_test_case :
       raw_test_file().test_cases) {
    tests.push_back(TestCaseHandle(
        index,
        absl::StrCat(file_label, "line_", raw_test_case.start_line_number),
        /*skip_by_sharding=*/false));
    ++index;
  }
  return tests;
}

ShardingEnvironment ShardingEnvironment::FromEnv() {
  int total_shards = 1;
  char* test_total_shards_string = getenv("TEST_TOTAL_SHARDS");
  if (test_total_shards_string != nullptr) {
    CHECK(absl::SimpleAtoi(test_total_shards_string, &total_shards))
        << test_total_shards_string;
  }

  int shard_index = 0;
  char* test_shard_index_string = getenv("TEST_SHARD_INDEX");
  if (test_shard_index_string != nullptr) {
    CHECK(absl::SimpleAtoi(test_shard_index_string, &shard_index))
        << test_shard_index_string;
  }
  CHECK_GE(total_shards, 1);
  CHECK_GE(shard_index, 0);
  CHECK_LT(shard_index, total_shards);

  ShardingEnvironment env;
  env.is_sharded_ = total_shards > 1;
  env.this_shard_ = shard_index;
  env.total_shards_ = total_shards;
  return env;
}

bool TestFile::ContainsAlternations() const {
  for (const internal::RawTestCase& test_case : raw_test_file().test_cases) {
    if (!test_case.alternation_info.groups.empty()) {
      return true;
    }
  }
  return false;
}

absl::StatusOr<std::vector<TestCaseHandle>> TestFile::ShardedTests(
    absl::FunctionRef<bool(const TestCaseInput&)> has_side_effects_fn,
    ShardingEnvironment sharding_environment) {
  if (ContainsAlternations()) {
    return absl::UnimplementedError(
        "TestFile::ShardedTests doesn't support alternations yet");
  }

  std::vector<TestCaseHandle> all_test_cases = Tests();
  if (!sharding_environment.is_sharded()) {
    return all_test_cases;
  }

  const int total_shards = sharding_environment.total_shards();
  const int this_shard = sharding_environment.this_shard();

  std::vector<TestCaseHandle> output;
  output.reserve(all_test_cases.size() * total_shards);

  // Googletest is expecting N test-cases, and will shard those via a simple
  // round robin. We purposely attempt to defeat this by create |total_shards|
  // copies of each test. This means that every test-case _will_ run once
  // on each shard.
  //
  // If a test would have been run by Googletest based on sharding, then we
  // given it a nice name and run it normally.
  // If a test would _not_ have been run by Googletest based sharding then
  // we do one of the following:
  // 1. If the test has side effects (according to `has_side_effects_fn`)
  //    Then we just run the test.
  // 2. If the tests _does not_ have side effects we will echo it's expected
  //    output so that the produced golden file will look correct.
  //
  // Note, there are other ways of handling #2 - such as improving the
  // merge tool, or using some final 'cleanup' phase to echo any intentionally
  // missing outputs due to sharding.

  for (int test_num = 0; test_num < all_test_cases.size(); ++test_num) {
    const TestCaseHandle& test_case = all_test_cases[test_num];
    FILE_BASED_TEST_DRIVER_RET_CHECK(!test_case.skip_by_sharding_);
    FILE_BASED_TEST_DRIVER_RET_CHECK_LT(test_case.index_, raw_test_file().test_cases.size());

    const internal::RawTestCase& raw_test_case =
        raw_test_file().test_cases[test_case.index_];
    FILE_BASED_TEST_DRIVER_RET_CHECK(raw_test_case.alternation_info.groups.empty())
        << "Alterations and are not yet supported in ShardedTests";

    bool always_run = has_side_effects_fn(TestCaseInput(
        filename(), test_case.index_, raw_test_case.start_line_number,
        raw_test_case.input_part.text));

    // We are reverse engineering googletests's roundrobin sharding.
    const bool would_have_been_skipped_by_googletest_sharding =
        (test_num % total_shards) != this_shard;

    for (int shard = 0; shard < total_shards; ++shard) {
      bool will_be_skipped_by_googletest_sharding =
          output.size() % total_shards != this_shard;
      bool skip_by_sharding =
          !always_run && would_have_been_skipped_by_googletest_sharding;

      std::string name = test_case.name_;

      if (skip_by_sharding) {
        name = absl::StrCat("skipped_", name, "_", output.size());
      }
      if (will_be_skipped_by_googletest_sharding) {
        // Googletest uses round robin, so within this_shard, it will completely
        // ignore this test except for checking name uniqueness.
        name =
            absl::StrCat("fake_test_should_be_skipped_by_googletest_sharding_",
                         name, "_", output.size());
      }

      output.emplace_back(TestCaseHandle(
          test_case.index_, name,
          skip_by_sharding || will_be_skipped_by_googletest_sharding));
    }
  }
  return output;
}

std::unique_ptr<TestFileRunner> TestFile::MakeRunner(
    FileBasedTestDriverConfig config) const {
  return absl::WrapUnique(new TestFileRunner(*this, std::move(config)));
}

const std::string& TestFile::filename() const {
  return raw_test_file().filename;
}

absl::StatusOr<absl::Nonnull<RunTestCaseOutput*>> TestFileRunner::all_output() {
  FILE_BASED_TEST_DRIVER_RET_CHECK(all_output_with_modes_ == nullptr)
      << "Cannot mix RunTestCase and RunTestCaseWithModes";
  if (all_output_ == nullptr) {
    all_output_ = std::make_unique<RunTestCaseOutput>();
  }
  return all_output_.get();
}

absl::StatusOr<absl::Nonnull<RunTestCaseWithModesOutput*>>
TestFileRunner::all_output_with_modes() {
  FILE_BASED_TEST_DRIVER_RET_CHECK(all_output_ == nullptr)
      << "Cannot mix RunTestCase and RunTestCaseWithModes";
  if (all_output_with_modes_ == nullptr) {
    all_output_with_modes_ = std::make_unique<RunTestCaseWithModesOutput>();
  }
  return all_output_with_modes_.get();
}

TestFileRunner::TestFileRunner(TestFile file, FileBasedTestDriverConfig config)
    : file_(std::move(file)), config_(std::move(config)) {}

static void GoogletestSkip() { GTEST_SKIP(); }

bool TestFileRunner::RunTestCase(
    TestCaseHandle test_case,
    absl::FunctionRef<void(absl::string_view test_input,
                           RunTestCaseResult* test_case_result /*out*/)>
        test_case_runner) {
  const internal::RawTestCase& raw_test_case =
      file_.raw_test_file().test_cases[test_case.index_];

  auto all_output_ptr = all_output();
  CHECK_OK(all_output_ptr.status());
  if (test_case.skip_by_sharding_) {
    GoogletestSkip();
    return !RunOneTestCase(
        raw_test_case,
        [](absl::string_view query_view,
           file_based_test_driver::RunTestCaseResult* test_result) {
          test_result->set_ignore_test_output(true);
        },
        config_, *all_output_ptr);
  }
  return !RunOneTestCase(raw_test_case, test_case_runner, config_,
                         *all_output_ptr);
}

bool TestFileRunner::RunTestCaseWithModes(
    TestCaseHandle test_case,
    absl::FunctionRef<
        void(absl::string_view test_input,
             RunTestCaseWithModesResult* test_case_result /* out */)>
        test_case_runner) {
  const internal::RawTestCase& raw_test_case =
      file_.raw_test_file().test_cases[test_case.index_];

  auto all_output_ptr = all_output_with_modes();
  CHECK_OK(all_output_ptr.status());
  if (test_case.skip_by_sharding_) {
    GoogletestSkip();
    return !RunOneTestCase(
        raw_test_case,
        [](absl::string_view query_view,
           file_based_test_driver::RunTestCaseWithModesResult* test_result) {
          test_result->set_ignore_test_output(true);
        },
        config_, *all_output_ptr);
  }
  return !RunOneTestCase(raw_test_case, test_case_runner, config_,
                         *all_output_ptr);
}

TestFileRunner::~TestFileRunner() {
  if (absl::GetFlag(FLAGS_file_based_test_driver_generate_test_output)) {
    if (all_output_ != nullptr) {
      all_output_->AddOutputToLog(std::string(file_.filename()));
    }
    if (all_output_with_modes_ != nullptr) {
      all_output_with_modes_->AddOutputToLog(std::string(file_.filename()));
    }
  }
}

bool RunTestCasesFromFiles(
    absl::string_view filespec,
    absl::FunctionRef<void(absl::string_view test_input,
                           RunTestCaseResult* test_case_result /*out*/)>
        run_test_case,
    FileBasedTestDriverConfig config) {
  auto wrapped_run_test_case = run_test_case;
  return RunTestCasesFromFiles<RunTestCaseResult>(
      filespec, wrapped_run_test_case, config);
}

bool RunTestCasesWithModesFromFiles(
    absl::string_view filespec,
    absl::FunctionRef<
        void(absl::string_view test_input,
             RunTestCaseWithModesResult* test_case_result /*out*/)>
        run_test_case,
    FileBasedTestDriverConfig config) {
  auto wrapped_run_test_case = run_test_case;
  return RunTestCasesFromFiles<RunTestCaseWithModesResult>(
      filespec, wrapped_run_test_case, config);
}

int64_t CountTestCasesInFiles(absl::string_view filespec) {
  std::vector<std::string> test_files;
  CHECK_OK(internal::Match(filespec, &test_files))
      << "Unable to find files matching " << filespec;
  int total_num_queries = 0;
  for (const std::string& file : test_files) {
    std::vector<std::string> lines;
    file_based_test_driver::internal::ReadTestFile(file, &lines);
    int line_number = 0;
    while (line_number < static_cast<int>(lines.size())) {
      using file_based_test_driver::internal::TestCasePartComments;
      std::vector<std::string> parts;
      std::vector<TestCasePartComments> comments;
      CHECK_OK(file_based_test_driver::internal::GetNextTestCase(
          lines, &line_number, &parts, &comments));
      ++total_num_queries;
    }
  }
  return total_num_queries;
}

}  // namespace file_based_test_driver

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
#include <iostream>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include "file_based_test_driver/base/logging.h"
#include "gtest/gtest.h"
#include "absl/flags/flag.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "file_based_test_driver/alternations.h"
#include "file_based_test_driver/base/file_util.h"
#include "file_based_test_driver/base/unified_diff.h"
#include "re2_st/re2.h"
#include "file_based_test_driver/base/ret_check.h"
#include "file_based_test_driver/base/status.h"

ABSL_FLAG(int32_t, file_based_test_driver_insert_leading_blank_lines, 0,
          "If this is set to N > 0, then file_based_test_driver will "
          "add leading blank lines to every test case that does not "
          "have N leading blank lines and that is not at the top of the "
          "test file. If a test already has M leading blank lines, then "
          "the number of blank lines added will be N - M.");
ABSL_FLAG(int32_t, file_based_test_driver_insert_split_suggestions_seconds, 0,
          "If this is set to a value N > 0, file_based_test_driver will "
          "emit a split suggestion point into the output after every N "
          "seconds worth of tests. This can be used to determine suitable "
          "split points when test files become too large and too slow to "
          "run.");
ABSL_FLAG(std::string, file_based_test_driver_ignore_regex, "",
          "If this flag is set then all substrings matching this "
          "pattern are replaced with a fixed string on a copy of the "
          "expected output and generated output for diffing.");
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

// Firebolt Start
ABSL_FLAG(bool, fb_write_actual, true,
          "If true, a test failing in <testfile> will generate the actual"
          "test result in <testfile>_actual.");
// Firebolt End

namespace {
template <typename RunTestCaseResultType>
using RunTestCallback =
    absl::FunctionRef<void(absl::string_view, RunTestCaseResultType*)>;
constexpr size_t kLogBufferSize =
    15000;
constexpr absl::string_view kRootDir =
    "";

// Firebolt Start

// Returns if this is a new test file to write actual results into.
// This makes it easy to make sure multiple wrong results from a single file
// get written out.
bool isNewTestFile(std::string filename) {

  static std::unordered_set<std::string> seen{};

  if (seen.contains(filename)) {
    return false;
  } else {
    seen.insert(std::move(filename));
    return true;
  }

}

// Firebolt End

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
    FILE_BASED_TEST_DRIVER_LOG(FATAL) << "Unable to read: " << filename << ". Failure: " << status;
  }
  *lines = SplitTestFileData(file_data);
}

absl::Status GetNextTestCase(const std::vector<std::string>& lines,
                             int* line_number /* input and output */,
                             std::vector<std::string>* parts,
                             std::vector<TestCasePartComments>* comments) {
  parts->clear();
  comments->clear();
  std::string current_part;
  std::string current_comment_start;
  std::string current_comment_end;
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

    // "--" is the separator between test case elements.
    // This code first does a very cheap check (StartsWith) before proceeding
    // with the more expensive RE2 check; don't remove the cheap check unless
    // the replacement is also cheap!
    if (absl::StartsWith(line, "--") && re2_st::RE2::FullMatch(line, "\\-\\-\\s*")) {
      parts->push_back(current_part);
      comments->push_back({current_comment_start, current_comment_end});
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
          parts->empty()) {
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
  parts->push_back(current_part);
  comments->push_back({current_comment_start, current_comment_end});
  return absl::OkStatus();
}

// For each line in 'lines', replaces 'needle' by 'replacement' if it occurs
// at the start of a line. Requires that each line is terminated with an \n.
static void ReplaceAtStartOfLine(absl::string_view needle,
                                 absl::string_view replacement,
                                 std::string* lines) {
  if (lines->empty()) return;
  FILE_BASED_TEST_DRIVER_CHECK(absl::EndsWith(*lines, "\n"));
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
  FILE_BASED_TEST_DRIVER_CHECK(absl::EndsWith(*lines, "\n"));
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
    std::vector<TestCasePartComments>* comments, std::string* all_output) {
  bool found_diffs = false;

  // If the file_based_test_driver_ignore_regex flag is set, then
  // known regex patterns are replaced with a fixed
  // string on both the left and right of the diff function.
  // This is useful when some known differences have to be ignored.
  // This feature is disabled by default.
  std::string output_string_for_diff = output_string;
  std::string expected_string_for_diff = expected_string;
  if (!absl::GetFlag(FLAGS_file_based_test_driver_ignore_regex).empty()) {
    re2_st::RE2::GlobalReplace(&output_string_for_diff,
                       absl::GetFlag(FLAGS_file_based_test_driver_ignore_regex),
                       "");
    re2_st::RE2::GlobalReplace(&expected_string_for_diff,
                       absl::GetFlag(FLAGS_file_based_test_driver_ignore_regex),
                       "");
  }

  if (expected_string_for_diff != expected_string) {
    FILE_BASED_TEST_DRIVER_LOG(WARNING) << "Expected output is modified for diff because of "
                 << "file_based_test_driver_ignore_regex flag";
  }

  if (output_string_for_diff != output_string) {
    FILE_BASED_TEST_DRIVER_LOG(WARNING) << "Generated Output from test case is modified for diff "
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
    found_diffs = true;
    if (absl::GetFlag(FLAGS_file_based_test_driver_individual_tests)) {
      // EXPECT_EQ does its own diff, but escapes carriage returns.
      ADD_FAILURE()
          << "\n\n******************* BEGIN TEST DIFF ********************"
          << "\nFailure in " << filename << ", line " << start_line_number + 1
          << ":\n"
          << "\n=================== DIFF ===============================\n"
          << diff;
      // Separate log message to avoid truncation in the case of long
      // output.
      ADD_FAILURE()
          << "=================== EXPECTED ===========================\n"
          << expected_string
          << "=================== ACTUAL =============================\n"
          << output_string
          << "******************* END TEST DIFF **********************\n\n";
    } else {
      FILE_BASED_TEST_DRIVER_LOG(WARNING)
          << "\n\n******************* BEGIN TEST DIFF ********************"
          << "\nFailure in " << filename
          << ", line " << start_line_number + 1 << ":\n"
          << "=================== EXPECTED ===========================\n"
          << expected_string
          << "=================== ACTUAL =============================\n"
          << output_string;

      // Separate log message to avoid truncation in the case of long
      // output.
      FILE_BASED_TEST_DRIVER_LOG(WARNING)
          << "\n=================== DIFF ===============================\n"
          << diff
          << "******************* END TEST DIFF **********************\n\n";
    }
  }

  // Firebolt Start
  std::ofstream actual;
  if (absl::GetFlag(FLAGS_fb_write_actual)) {
    // Figure out if we need to create a new _actual file or can use
    // the existing one.
    std::string out_file = std::string(filename) + "_actual";
    auto mode =
        isNewTestFile(out_file) ? std::ios_base::out : std::ios_base::app;

    // Write results to the file.
    actual.open(std::string(filename) + "_actual", mode);
  }
  // Firebolt End

  // Add to all_output.
  if (!all_output->empty()) {
    absl::StrAppend(all_output, "==\n");
    // Firebolt Start
    if (absl::GetFlag(FLAGS_fb_write_actual)) {
      actual << "==\n";
    }
    // Firebolt End
  }
  if (matches_requested_same_as_previous) {
    absl::StrAppend(all_output,
                    internal::BuildTestFileEntry(
                        {test_string, "[SAME AS PREVIOUS]\n"}, *comments));
  } else {
    absl::StrAppend(all_output, output_string);
  }
  // Firebolt Start
  if (absl::GetFlag(FLAGS_fb_write_actual)) {
    actual << output_string;
    actual.close();
  }
  // Firebolt End

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

template <class RunTestCaseResultType>
absl::Status RunAlternations(
    RunTestCaseResultType* result,
    RunTestCallback<RunTestCaseResultType> run_test_case) {
  FILE_BASED_TEST_DRIVER_RET_CHECK(result->IsEmpty());

  // Calculate expanded values for the cross product of all alternations
  // of the form {{a|b}} in parts_[0].

  // A vector of alternation group values and the test case strings
  // expanded with those values.
  std::vector<std::pair<std::string, std::string>>
      alternation_values_and_expanded_inputs;
  internal::BreakStringIntoAlternations(
      result->parts()[0], &alternation_values_and_expanded_inputs);

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
      FILE_BASED_TEST_DRIVER_LOG(INFO) << "Running alternation " << test_alternation;
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

// Regex for finding alternation group. Uses ".*?" so inner matches are
// nongreedy -- we want to get the shortest match, not the longest one.
static char kRegexAlternationGroup[] = "(\\{\\{(.*?)\\}\\})";
static const re2_st::LazyRE2 alternation_group_matcher =
    {kRegexAlternationGroup};

// Replace the first occurance of `oldsub` with `newsub` in `s`.  If s oldsub
// is empty or `oldsub` is not found, returns `s`.
static std::string StringReplace(absl::string_view s, absl::string_view oldsub,
                                 absl::string_view newsub) {
  std::string out;
  if (oldsub.empty()) {
    return std::string(s);  // If empty, append the given string.
  }

  absl::string_view::size_type pos = s.find(oldsub);
  if (pos == absl::string_view::npos) {
    // Didn't find it, return input.
    return std::string(s);
  }

  out.append(s.data(), pos);
  out.append(newsub.data(), newsub.length());
  absl::string_view::size_type start_pos = pos + oldsub.length();
  out.append(s.data() + start_pos, s.length() - start_pos);
  return out;
}

// Recursive implementation for BreakStringIntoAlternations(). Expands one
// alternation from <input> and recurses for each alternation value, adding the
// chosen alternation value to selected_alternation_values. If there are no more
// alternations in <input>, adds (<selected_alternation_values>, <input>) to
// <alternation_values_and_expanded_inputs>.
static void BreakStringIntoAlternationsImpl(
    absl::string_view input,
    std::vector<std::pair<std::string, std::string>>*
        alternation_values_and_expanded_inputs,
    const std::string& selected_alternation_values) {
  // Identify one alternation group surrounded by "{{ }}".
  re2_st::StringPiece alternation_group_match[3];
  absl::string_view input_sp(input);
  // If alternation is not found, add input to output.
  if (!alternation_group_matcher->Match(
          input_sp, 0 /* startpos */, input_sp.size(), re2_st::RE2::UNANCHORED,
          &alternation_group_match[0], 3 /* nmatch */)) {
    alternation_values_and_expanded_inputs->emplace_back(
        selected_alternation_values, input);
    return;
  }
  // Identify values in alternation. alternation_group_match[2] is the submatch
  // that has the contents of the {{}}.
  const std::vector<std::string> alternation_values =
      absl::StrSplit(alternation_group_match[2].data(), '|', absl::AllowEmpty());

  // For each alternation value, replace the first alternation group in <input>
  // with that value and recurse to expand the next alternation in <input>.
  for (const std::string& alternation_value : alternation_values) {
    // Replaces the 1st occurrence of alternation_group_match[1] => "{{...}}"".
    const std::string substituted_input =
        StringReplace(input, alternation_group_match[1].data(), alternation_value);

    // Recurse for expanding additional alternation groups.
    if (selected_alternation_values.empty()) {
      BreakStringIntoAlternationsImpl(substituted_input,
                                      alternation_values_and_expanded_inputs,
                                      alternation_value);
    } else {
      BreakStringIntoAlternationsImpl(
          substituted_input, alternation_values_and_expanded_inputs,
          absl::StrCat(selected_alternation_values, ",", alternation_value));
    }
  }
}

void BreakStringIntoAlternations(
    absl::string_view input, std::vector<std::pair<std::string, std::string>>*
                                 alternation_values_and_expanded_inputs) {
  alternation_values_and_expanded_inputs->clear();
  BreakStringIntoAlternationsImpl(input,
                                  alternation_values_and_expanded_inputs, "");
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
  FILE_BASED_TEST_DRIVER_CHECK_EQ(output_lines.back(), "");
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
    FILE_BASED_TEST_DRIVER_LOG(INFO)
        << "\n"
        << "****" << test_output_prefix << "_BEGIN**** "
        << (first_output_block ? "NEW_TEST_RUN " : "")
        << file_path << "\n"
        << this_output
        << "****" << test_output_prefix << "_END****\n";
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
    prev_expected_outputs_ = absl::make_unique<TestCaseOutputs>(
        prev_expected_outputs);
    prev_merged_outputs_ = absl::make_unique<TestCaseOutputs>(
        prev_merged_outputs);
  }

 private:
  std::string all_actual_output_;
  std::string all_merged_output_;
  std::unique_ptr<TestCaseOutputs> prev_expected_outputs_;
  std::unique_ptr<TestCaseOutputs> prev_merged_outputs_;
};

// Runs one test case and compares its output with the expected output.
// Returns true if diff is found.
//   'filename': the file that contains the test case.
//   'start_line_number: the first line of the test case in 'filename'.
//   'parts': the first part is the test input; the remaining are expected test
//            output
//   'comments': comments for each part, including a 'before' comment and an
//               'after' comment.
//   'run_test_case': the callback to run the test
//   'all_output': class that keeps track of the output for all test cases.
template <typename RunTestCaseResultType, typename AllTestCasesOutput>
bool RunOneTestCase(absl::string_view filename, const int start_line_number,
                    std::vector<std::string>* parts,
                    std::vector<internal::TestCasePartComments>* comments,
                    RunTestCallback<RunTestCaseResultType> run_test_case,
                    AllTestCasesOutput* all_output);

template <typename RunTestCaseResultType, typename AllTestCasesOutput>
bool RunTestCasesFromOneFile(
    absl::string_view filename,
    RunTestCallback<RunTestCaseResultType> run_test_case) {
  bool found_diffs = false;
  std::vector<std::string> lines;
  internal::ReadTestFile(filename, &lines);

  FILE_BASED_TEST_DRIVER_LOG(INFO) << "Executing tests from file " << filename;

  // Keep track of section start time for emitting time based split
  // suggestions.
  absl::Time section_start_time = absl::Now();

  int line_number = 0;
  std::vector<std::string> parts;
  std::vector<internal::TestCasePartComments> comments;
  AllTestCasesOutput all_output;

  while (line_number < static_cast<int>(lines.size())) {
    // Insert split suggestions if requested by flag.
    if (absl::GetFlag(
            FLAGS_file_based_test_driver_insert_split_suggestions_seconds) >
            0 &&
        absl::Now() - section_start_time >=
            absl::Seconds(absl::GetFlag(
                FLAGS_file_based_test_driver_insert_split_suggestions_seconds))) {
      absl::StrAppend(all_output.GetAllOutput(),
                      "==\n####### SPLIT TEST FILE HERE ######\n");
      section_start_time = absl::Now();
      if (absl::GetFlag(FLAGS_file_based_test_driver_individual_tests)) {
        ADD_FAILURE() << "SPLIT TEST FILE HERE";
      }
      found_diffs = true;
    }

    const int start_line_number = line_number;
    FILE_BASED_TEST_DRIVER_CHECK_OK(internal::GetNextTestCase(lines, &line_number, &parts, &comments));
    FILE_BASED_TEST_DRIVER_CHECK(!parts.empty());

    found_diffs |= RunOneTestCase(filename, start_line_number, &parts,
                                  &comments, run_test_case, &all_output);
  }
  all_output.AddOutputToLog(std::string(filename));
  return !found_diffs;
}

// Template function that implements RunTestCasesFromFiles and
// RunTestCasesWithModesFromFiles. See comments in file_based_test_driver.h for
// more details.
template <typename RunTestCaseResultType, typename AllTestCasesOutput>
bool RunTestCasesFromFiles(
    absl::string_view filespec,
    RunTestCallback<RunTestCaseResultType> run_test_case) {
  bool no_diffs = true;
  std::vector<std::string> test_files;
  FILE_BASED_TEST_DRIVER_CHECK_OK(internal::Match(filespec, &test_files)) << "Filespec " << filespec;
  FILE_BASED_TEST_DRIVER_CHECK_GT(test_files.size(), size_t{0}) << "Filespec " << filespec;
  for (const std::string& filename : test_files) {
    no_diffs &=
        RunTestCasesFromOneFile<RunTestCaseResultType, AllTestCasesOutput>(
            filename, run_test_case);
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
    FILE_BASED_TEST_DRIVER_CHECK(!comments->empty());  // The test case is always present.
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
        FILE_BASED_TEST_DRIVER_LOG(INFO) << "Test without leading blank line in " << filename
                  << ", line " << start_line_number + 1;
      }
      added_lines = true;
    }
  }
  return added_lines;
}

// Implements RunOneTestCase for regular test cases (RunTestCasesFromFiles).
// Returns true if differences were found.
template <>
bool RunOneTestCase<RunTestCaseResult, RunTestCaseOutput>(
    absl::string_view filename, const int start_line_number,
    std::vector<std::string>* parts,
    std::vector<internal::TestCasePartComments>* comments,
    RunTestCallback<RunTestCaseResult> run_test_case,
    RunTestCaseOutput* all_output) {
  FILE_BASED_TEST_DRIVER_CHECK(parts != nullptr);
  FILE_BASED_TEST_DRIVER_CHECK(comments != nullptr);
  FILE_BASED_TEST_DRIVER_CHECK(all_output != nullptr);
  FILE_BASED_TEST_DRIVER_CHECK(!parts->empty());
  // Run test.
  const std::string test_case_log =
      absl::StrCat("test case from ", filename, ", line ",
                   start_line_number + 1, ":\n", (*parts)[0]);
  bool ignore_test_output = false;
  bool matches_requested_same_as_previous = false;
  std::vector<std::string> output;
  if ((*parts)[0].empty() && parts->size() == 1) {
    // Skip empty test cases if there's no expected output.
    // If there is an expected output, then we'll try to run the test
    // with an empty input.
    FILE_BASED_TEST_DRIVER_LOG(INFO) << "Skipping empty test case from " << filename << ", line "
              << start_line_number + 1 << ".";
    output = *parts;
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
      FILE_BASED_TEST_DRIVER_LOG(INFO) << "Running " << test_case_log;
    }
    // Otherwise, we delay the logging until we know if the test is ignored.

    RunTestCaseResult test_result;
    // Pass file name, line number, and parts of the test along with
    // test_result.
    test_result.set_filename(std::string(filename));
    test_result.set_line(start_line_number + 1);
    test_result.set_parts(*parts);
    FILE_BASED_TEST_DRIVER_CHECK_OK(internal::RunAlternations(&test_result, run_test_case));
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
      FILE_BASED_TEST_DRIVER_LOG(INFO) << "Ignoring test result";
    }
    output = *parts;
    if (parts->size() == 2 && (*parts)[1] == kSameAsPrevious) {
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
    if (parts->size() == 2 && (*parts)[1] == kSameAsPrevious &&
        !all_output->prev_output().empty()) {
      parts->resize(1); /* keep input */
      parts->insert(parts->end(),
                    all_output->prev_output().begin() + 1 /* skip input */,
                    all_output->prev_output().end());
      if (output == *parts) {
        matches_requested_same_as_previous = true;
      }
    }

    if (!absl::GetFlag(FLAGS_file_based_test_driver_log_ignored_test)) {
      FILE_BASED_TEST_DRIVER_LOG(INFO) << "Executed " << test_case_log;
    }
  }

  if (update_prev_output) {
    all_output->set_prev_output(output);
  }

  const bool added_blank_lines =
      AddBlankLines(filename, start_line_number, comments);

  const std::string output_string =
      internal::BuildTestFileEntry(output, *comments);
  const std::string expected_string =
      internal::BuildTestFileEntry(*parts, *comments);

  return internal::CompareAndAppendOutput(
             expected_string, output_string, (*parts)[0],
             matches_requested_same_as_previous, filename, start_line_number,
             comments, all_output->GetAllOutput()) ||
         added_blank_lines;
}

// Implements RunOneTestCase for test cases with modes
// (RunTestCasesWithModesFromFiles).
// Returns true if differences were found.
template <>
bool RunOneTestCase<RunTestCaseWithModesResult, RunTestCaseWithModesOutput>(
    absl::string_view filename, const int start_line_number,
    std::vector<std::string>* parts,
    std::vector<internal::TestCasePartComments>* comments,
    RunTestCallback<RunTestCaseWithModesResult> run_test_case,
    RunTestCaseWithModesOutput* all_output) {
  FILE_BASED_TEST_DRIVER_CHECK(parts != nullptr);
  FILE_BASED_TEST_DRIVER_CHECK(comments != nullptr);
  FILE_BASED_TEST_DRIVER_CHECK(all_output != nullptr);
  FILE_BASED_TEST_DRIVER_CHECK(!parts->empty());

  const std::string test_case_log =
      absl::StrCat("test case from ", filename, ", line ",
                   start_line_number + 1, ":\n", (*parts)[0]);
  bool ignore_test_output = false;
  bool matches_requested_same_as_previous = false;
  const std::string kSameAsPrevious = "[SAME AS PREVIOUS]\n";
  TestCaseOutputs expected_outputs;
  TestCaseOutputs merged_outputs;
  RunTestCaseWithModesResult test_result;

  if ((*parts)[0].empty() && parts->size() == 1) {
    // Skip empty test cases if there's no expected output.
    // If there is an expected output, then we'll try to run the test
    // with an empty input.
    FILE_BASED_TEST_DRIVER_LOG(INFO) << "Skipping empty test case from " << filename << ", line "
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
      FILE_BASED_TEST_DRIVER_LOG(INFO) << "Running " << test_case_log;
    }
    // Otherwise, we delay the logging until we know if the test is ignored.

    if (parts->size() == 2 && (*parts)[1] == kSameAsPrevious &&
        all_output->prev_expected_outputs() != nullptr) {
      expected_outputs = *all_output->prev_expected_outputs();
    } else {
      const std::vector<std::string> outputs(parts->begin() + 1, parts->end());
      FILE_BASED_TEST_DRIVER_CHECK_OK(expected_outputs.ParseFrom(outputs));
    }

    // Pass file name, line number, and parts of the test along with
    // test_result.
    test_result.set_filename(std::string(filename));
    test_result.set_line(start_line_number + 1);
    test_result.set_parts(*parts);
    FILE_BASED_TEST_DRIVER_CHECK_OK(internal::RunAlternations(&test_result, run_test_case));
    if (test_result.ignore_test_output()) {
      ignore_test_output = true;
    } else {
      FILE_BASED_TEST_DRIVER_CHECK_OK(TestCaseOutputs::MergeOutputs(expected_outputs,
                                             {test_result.test_case_outputs()},
                                             &merged_outputs));
    }
  }

  // If the callback set the 'ignore_test_output' boolean to true,
  // ignore this test's output. We still pretend the output was correct,
  // so that the generated output file has all the skipped parts as well.
  // This helps in diffing the output with the original.
  bool update_prev_output = true;
  if (ignore_test_output) {
    if (absl::GetFlag(FLAGS_file_based_test_driver_log_ignored_test)) {
      FILE_BASED_TEST_DRIVER_LOG(INFO) << "Ignoring test result";
    }
    merged_outputs = expected_outputs;
    if (parts->size() == 2 && (*parts)[1] == kSameAsPrevious) {
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
    if (parts->size() == 2 && (*parts)[1] == kSameAsPrevious &&
        all_output->prev_merged_outputs() != nullptr) {
      if (*all_output->prev_merged_outputs() == merged_outputs) {
        matches_requested_same_as_previous = true;
      }
    }

    if (!absl::GetFlag(FLAGS_file_based_test_driver_log_ignored_test)) {
      FILE_BASED_TEST_DRIVER_LOG(INFO) << "Executed " << test_case_log;
    }
  }

  if (update_prev_output) {
    all_output->ResetOutputs(expected_outputs, merged_outputs);
  }

  std::vector<std::string> expected_outputs_parts;
  expected_outputs_parts.push_back((*parts)[0]);
  FILE_BASED_TEST_DRIVER_CHECK_OK(expected_outputs.GetCombinedOutputs(
      false /* include_possible_modes */, &expected_outputs_parts));

  std::vector<std::string> merged_outputs_parts;
  merged_outputs_parts.push_back((*parts)[0]);
  FILE_BASED_TEST_DRIVER_CHECK_OK(merged_outputs.GetCombinedOutputs(
      false /* include_possible_modes */, &merged_outputs_parts));

  std::vector<std::string> actual_outputs_parts;
  actual_outputs_parts.push_back((*parts)[0]);
  FILE_BASED_TEST_DRIVER_CHECK_OK(test_result.test_case_outputs().GetCombinedOutputs(
      true /* include_possible_modes */, &actual_outputs_parts));

  const bool added_blank_lines =
      AddBlankLines(filename, start_line_number, comments);

  const std::string output_string =
      internal::BuildTestFileEntry(merged_outputs_parts, *comments);
  const std::string expected_string =
      internal::BuildTestFileEntry(expected_outputs_parts, *comments);
  const std::string actual_output_string =
      internal::BuildTestFileEntry(actual_outputs_parts, *comments);

  bool found_diffs =
      internal::CompareAndAppendOutput(
          expected_string, output_string, (*parts)[0],
          matches_requested_same_as_previous, filename, start_line_number,
          comments, all_output->GetAllOutput()) ||
      added_blank_lines;

  if (!all_output->all_actual_output().empty()) {
    absl::StrAppend(all_output->mutable_all_actual_output(), "==\n");
  }
  absl::StrAppend(all_output->mutable_all_actual_output(),
                  actual_output_string);
  return found_diffs;
}

}  // namespace

bool RunTestCasesFromFiles(
    absl::string_view filespec,
    absl::FunctionRef<void(absl::string_view test_input,
                           RunTestCaseResult* test_case_result /*out*/)>
        run_test_case) {
  auto wrapped_run_test_case = run_test_case;

  return RunTestCasesFromFiles<RunTestCaseResult, RunTestCaseOutput>(
      filespec, wrapped_run_test_case);
}

bool RunTestCasesWithModesFromFiles(
    absl::string_view filespec,
    absl::FunctionRef<
        void(absl::string_view test_input,
             RunTestCaseWithModesResult* test_case_result /*out*/)>
        run_test_case) {
  auto wrapped_run_test_case = run_test_case;
  return RunTestCasesFromFiles<RunTestCaseWithModesResult,
                               RunTestCaseWithModesOutput>(
      filespec, wrapped_run_test_case);
}

int64_t CountTestCasesInFiles(absl::string_view filespec) {
  std::vector<std::string> test_files;
  FILE_BASED_TEST_DRIVER_CHECK_OK(internal::Match(filespec, &test_files))
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
      FILE_BASED_TEST_DRIVER_CHECK_OK(file_based_test_driver::internal::GetNextTestCase(
          lines, &line_number, &parts, &comments));
      ++total_num_queries;
    }
  }
  return total_num_queries;
}

}  // namespace file_based_test_driver

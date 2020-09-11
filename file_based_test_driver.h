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

// A test driver that runs tests specified in text files. This can be used as
// part of googletest unit tests or other test suites.
//
// For some more in-depth examples, an example test file and a googletest unit
// test that uses file_based_test_driver and test_options can be found in
// example.test and example_test.cc.
//
// Basic description:
// * The test files contain multiple test cases. Each test case has an input
//   block, followed by several expected output blocks. The format of the test
//   files is specified below.
// * The test driver runs the tests one by one, by calling a callback that you
//   specify. When the output differs from the expected output, it writes the
//   actual and expected output to the log, as well as the diffs.
// * After running the tests, the test driver generates output that contains the
//   test cases and the *actual* outputs, in the same format as the input file.
//   The output is written to the INFO log, annotated with the test input file
//   name.
// * The test driver returns a true/false success state. If you call the test
//   driver from a googletest unit test, you must wrap the call in
//   EXPECT_TRUE().
//
// This test driver is frequently used together with the TestCaseOptions class.
// This class allow you to easily annotate individual test cases with custom
// options which control the test environment (e.g. flags or other settings).
//
//
// This is an example of the contents of a test input file. By convention, these
// files are named with the extension .test:
//
// # Comment
// First test case.
// A test case can have multiple lines.
// --
// # This is a test output
// Test output 1
// --
// ...
// --
// Test output N
// ==
// # Comment
// Second {{test|TEST}} case.
// --
// Second test case output.
// ==
// # Comment
// Third test case.
// --
// [SAME AS PREVIOUS]
//
//
// Detailed notes about the format:
// * Tests are separated by a line containing "==". The test case and the test
//   outputs are separated by "--".
// * Backslashes can be used at the start of a line to escape special
//   meanings, e.g. "\--", "\==", "\#" and "\\".
// * Lines that start with # are comments. Comments can appear at the start or
//   end of each test case, and at the start or end of outputs. Blank lines at
//   the start or end of a test case or output are also treated as comments.
//   Comments may not appear in the middle of test cases or outputs.
// * Comments are preserved in the output of the test driver, for the test
//   case as well as the outputs. They are not passed into the callback that
//   runs the actual test, and they are also not part of the comparison of
//   outputs.
// * The special output [SAME AS PREVIOUS] can be used to indicate that a test
//   output should always be exactly the same as the output for the preceding
//   test. This can be used in situations where two tests are semantically
//   supposed to give exactly the same results, e.g. when one test case uses a
//   shorthand syntax for a particular feature and the other one specifies the
//   expanded syntax. When the test output is [SAME AS PREVIOUS] and the test
//   case output matches the expected input, the test driver will write
//   [SAME AS PREVIOUS] to the output file as well. It will *never* write this
//   to the output if two subsequent test cases match by coincidence: this
//   feature is intended as a manual, human-specified annotation that indicates
//   a semantic test case equivalence, not an accidental equivalence.
// * A test case can specify multiple equivalent test runs using "alternations",
//   which are written as {{a|b}}. This example signifies an alternation of
//   "a" and "b". The test runner calls the test callback with all combinations
//   of alternation values, merges identical results into groups, and outputs
//   separate test results for each such group, annotated with the alternation
//   values that were used.
//
// Besides the basic functionality, the test driver also supports running a test
// case in multiple 'modes' using the same test file
// (RunTestCasesWithModesFromFiles).
//
// * This test framework is not aware of the set of possible modes. Each test
//   case can have outputs for one or more modes. The test callback choose which
//   mode(s) to run the test in (e.g. based on a flag value) and produces output
//   for that mode(s). For every mode present in the callback's output, the test
//   framework compares the actual and expected output for that mode. Expected
//   outputs for modes that aren't in the actual output are ignored.
//     For instance, a test could be run with both an old and a new
//     implementation of a system-under-test. The test output for the test case
//     run with the old system would be emitted in mode "OLD_IMPL", and the
//     test output for the same test case run with the new system would be
//     emitted in mode "NEW_IMPL".
// * In the test file, each test case still has an input block, followed by
//   several expected output blocks. The output blocks should include the
//   results of all the test modes. See below for detailed formats.
//
// The test driver also supports multiple named outputs for a test. A named
// output is annotated with a 'result type'. Each mode can generate multiple
// named outputs. Each mode can also generate one unnamed test output, with an
// empty 'result type'. The set of result types is allowed to be different
// across modes.
//   For instance, the old implementation of a system-under-test does some
//   computation based on the test input, and the "OLD_IMPL" mode outputs
//   the computation result as the unnamed output. The new implementation of the
//   system does the computation and also some extra work. Therefore the
//   "NEW_IMPL" mode may generate two test outputs: the unnamed
//   output for the computation result and a named output (<EXTRA OUTPUT>)
//   for any extra output.
//
// Examples of expected test outputs for tests with modes.
//
// # A test with single output. This means all test modes generate the same
// # output.
// test input
// --
// main test output
// ==
//
// # A test with multiple outputs, each annotated with a 'result type'
// # (enclosed in <>). You can have a 'main output' with empty result type.
// test input
// --
// main test output
// --
// <result type 1>
// test output for result type 1
// --
// <result type 2>
// test output for result type 2
// ==
//
// # A single output test case with two modes. Different test modes generate
// # different outputs. Modes are enclosed in []. Note the empty result type
// # annotation (<>) is required when modes are specified in the test. This is
// # because sometimes the first line of the actual output is also enclosed in
// # [].
// test input
// --
// <>[MODE A]
// main test output for MODE A.
// --
// <>[MODE B]
// main test output for MODE B.
// ==
//
// # A test with three modes. Each mode generates multiple outputs.
// test input
// --
// <>[MODE A][MODE B]
// main test output for MODE A and MODE B.
// --
// <>[MODE C]
// main test output for MODE C.
// --
// <result type 1>[MODE A]
// result type 1 test output for MODE A.
// --
// <result type 2>[MODE B][MODE C]
// result type 2 test output for MODE B and MODE C.
// ==

#ifndef THIRD_PARTY_FILE_BASED_TEST_DRIVER_FILE_BASED_TEST_DRIVER_H_
#define THIRD_PARTY_FILE_BASED_TEST_DRIVER_FILE_BASED_TEST_DRIVER_H_

#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/attributes.h"
#include <cstdint>
#include "absl/flags/declare.h"
#include "absl/functional/function_ref.h"
#include "absl/memory/memory.h"
#include "absl/strings/string_view.h"
#include "run_test_case_result.h"

// Set this flag to a positive value to force each test case to start with a
// particular number of blank lines.
ABSL_DECLARE_FLAG(int32_t, file_based_test_driver_insert_leading_blank_lines);

// Set this flag to replace all substrings matching this pattern with a fixed
// string on a copy of the expected output and generated output for diffing.
ABSL_DECLARE_FLAG(std::string, file_based_test_driver_ignore_regex);

// Set this flag to enable calling EXPECT_EQ on every diff. This enables
// displaying individual diff failures in sponge.
ABSL_DECLARE_FLAG(bool, file_based_test_driver_individual_tests);

namespace file_based_test_driver {

// For every test file that matches <filespec>, this function:
// - Reads the file.
// - For every test case in the file, calls <run_test_case>, passing it the test
//   case input string in the <test_input> argument. The <run_test_case>
//   function should return in its <test_case_result> parameter the output of
//   the test. The output may consist of multiple parts, and it can be annotated
//   with options for the handling of the output.
// - Takes the test results from <run_test_case> and prints the differences with
//   the expected output taken from the input file.
// - Logs the entire test results in the same format as the input file, merging
//   in the comments from the input file where possible.
//
// If the callback calls RunTestCaseResult::set_ignore_test_output(true) on the
// <test_case_result>, then the test runner will pretend that the test output
// matched the expected output. This can be used to conditionally skip tests.
//
// The result is true if all test outputs matched the expected test outputs,
// false otherwise.
//
ABSL_MUST_USE_RESULT bool RunTestCasesFromFiles(
    absl::string_view filespec,
    absl::FunctionRef<void(absl::string_view test_input,
                           RunTestCaseResult* test_case_result /*out*/)>
        run_test_case);

// Writes <text> with a associated path 'file_path' to the INFO log.
//
// The function is used internally for output logging in RunTestCasesFromFile,
// but it can be used in more general applications where we want to log
// some outputs that can be extracted and written into files with a specific
// directory structure.
//
void LogExtractableText(absl::string_view file_path, absl::string_view text);

// This function is similar to RunTestCasesFromFiles except that it supports
// test modes for the test cases.
// The expected output of a test case should be a TestCaseOutputs in text format
// (see test_case_outputs.h for details). And <run_test_case> should also return
// a TestCaseOutputs. The returned TestCaseOutputs does not need to contain
// results for all test modes. This function will only compare the results for
// the test modes that are actually run.
//
// For every test file that matches <filespec>, this function:
// - Reads the file.
// - For every test case in the file: parse the expected output string into a
//   TestCaseOutputs.
// - Calls <run_test_case>, passing it the test case input string in the
//   <test_input> argument. The <run_test_case> should return in its
//   <test_case_result> parameter the output of the test, which contains a
//   TestCaseOutputs. <run_test_case> does not need to run all the test modes.
// - Merges the test outputs returned by <run_test_case> into the expected test
//   outputs.
// - Takes the merged test outputs and prints the differences with the expected
//   output.
// - Logs the actual test result returned in <test_case_result>. This will be
//   used to merge the results from different tests.
// - Also logs the merged test results in the same format as the input file,
//   merging in the comments from the input file where possible.
// Takes ownership of <run_test_case>. <run_test_case> must be a permanent
// callback.
//
// If the callback calls RunTestCaseResult::set_ignore_test_output(true) on the
// <test_case_result>, then the test runner will pretend that the test output
// matched the expected output. This can be used to conditionally skip tests.
//
// The result is true if all test outputs matched the expected test outputs,
// false otherwise.
//
ABSL_MUST_USE_RESULT bool RunTestCasesWithModesFromFiles(
    absl::string_view filespec,
    absl::FunctionRef<
        void(absl::string_view test_input,
             RunTestCaseWithModesResult* test_case_result /* out */)>
        run_test_case);

// Returns the number of individual test cases contained in 'filespec'. This is
// the number of invocations that would be made to 'run_test_case' if
// RunTestCaseFromFiles(filespec, run_test_case) were called.
int64_t CountTestCasesInFiles(absl::string_view filespec);

// Internal functions. Exposed here for unit testing purposes only, do not use
// directly.
namespace internal {

// Reads the raw file_data and returns the lines from the file data.
std::vector<std::string> SplitTestFileData(absl::string_view file_data);

// Reads <filename> and returns the lines from the file in <lines>.
void ReadTestFile(absl::string_view filename, std::vector<std::string>* lines);

// Comments associated with a test case part. Each test case part can have a
// comment at the start and at the end, but no comments can appear in the middle
// of the test case part.
struct TestCasePartComments {
  std::string start_comment;
  std::string end_comment;
};

inline bool operator<(const TestCasePartComments& lhs,
                      const TestCasePartComments& rhs) {
  if (lhs.start_comment < rhs.start_comment) {
    return true;
  }
  return lhs.end_comment < rhs.end_comment;
}

inline bool operator==(const TestCasePartComments& a,
                       const TestCasePartComments& b) {
  return a.start_comment == b.start_comment && a.end_comment == b.end_comment;
}

inline std::ostream& operator<<(std::ostream& out,
                                const TestCasePartComments& v) {
  return out << "{" << v.start_comment << "," << v.end_comment << "}";
}

// Extracts a test case from <lines>, starting from <line_number>.
// A test case section can have multiple parts separated by '--'. Each part
// may have comments at the beginning and the end. The parts are returned in
// <parts>, the comments in <comments> (at the same index). Existing elements in
// <parts> and <comments> are cleared. Is guaranteed to return at least one part
// if <*line_number> is less than lines.size().
void GetNextTestCase(const std::vector<std::string>& lines,
                     int* line_number /* input and output */,
                     std::vector<std::string>* parts,
                     std::vector<TestCasePartComments>* comments);

// Builds a test file entry from parts and their start and end comments. If
// there are less parts than comments, dumps the comments from the missing
// parts at the end with notes about missing parts. If there are less comments
// than parts, just writes the parts with no comments.
std::string BuildTestFileEntry(
    const std::vector<std::string>& parts,
    const std::vector<TestCasePartComments>& comments);

// Takes <input> and generates all combinations of alternation values specified
// inside the string. Returns the combinations as pairs in
// <alternation_values_and_expanded_inputs>, where the first element is the
// comma-separated list of selected alternation values, and the second element
// is <input> with those alternation values substituted.
//
// An alternation is defined in the string using the syntax "{{a|b}}". A string
// such as "a{{b|c}}{{d|e}}" has alternation value combinations "b,d", "b,e",
// "c,d", and "c,e". This corresponds to expanded values "abd", "abe", "acd",
// "ace". The returned vector for this string would contain ("b,d", "abd"),
// ("b,e", "abe"), and so on.
void BreakStringIntoAlternations(
    absl::string_view input, std::vector<std::pair<std::string, std::string>>*
                                 alternation_values_and_expanded_inputs);

}  // namespace internal
}  // namespace file_based_test_driver

#endif  // THIRD_PARTY_FILE_BASED_TEST_DRIVER_FILE_BASED_TEST_DRIVER_H_

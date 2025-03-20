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

#include <cstdint>
#include <functional>
#include <memory>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/base/nullability.h"
#include "absl/flags/declare.h"
#include "absl/functional/function_ref.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "file_based_test_driver/run_test_case_result.h"

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

// Diff information that can be used by client-code for analysis or rendering.
struct ResultDiff {
  // The unified diff being generated.
  std::string unified_diff;
  // The expected output string of the test case.
  std::string expected;
  // The actual output string of the test case.
  std::string actual;
  // Path of the test file relative to project root.
  std::string file_path;
  // The line number where test case is presented in the test file. Zero-based.
  int start_line_number = -1;
};

class FileBasedTestDriverConfig {
 public:
  using OnResultDiffFoundCallback = std::function<void(const ResultDiff&)>;

  FileBasedTestDriverConfig& set_alternations_enabled(bool v) {
    alternations_enabled_ = v;
    return *this;
  }

  FileBasedTestDriverConfig& set_on_result_diff_found_callback(
      const OnResultDiffFoundCallback& cb) {
    on_result_diff_found_ = cb;
    return *this;
  }

  // [default enabled] When set, enables execution of alternations (see below).
  // If disabled, alternations parsing is disabled. Useful if tests may include
  // {{}} type constructs.
  bool alternations_enabled() const { return alternations_enabled_; }
  const OnResultDiffFoundCallback& on_result_diff_found() const {
    return on_result_diff_found_;
  }

 private:
  bool alternations_enabled_ = true;
  // Custom callback when a diff is found. The callback is invoked exactly once
  // for each failed test case.
  // If alternations_enabled_ is `true` and any one alternation is causing a
  // diff, the callback will be called exactly once and the diffing is performed
  // against coalesced results.
  // See also "ALTERNATION GROUP".
  OnResultDiffFoundCallback on_result_diff_found_ = [](const ResultDiff&) {
  };  // No-op by default.
};

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
        run_test_case,
    FileBasedTestDriverConfig config = {});

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
        run_test_case,
    FileBasedTestDriverConfig config = {});

// Returns the number of individual test cases contained in 'filespec'. This is
// the number of invocations that would be made to 'run_test_case' if
// RunTestCaseFromFiles(filespec, run_test_case) were called.
int64_t CountTestCasesInFiles(absl::string_view filespec);

namespace internal {
class RawTestFile;
class RunTestCaseOutput;
class RunTestCaseWithModesOutput;

}  // namespace internal

// A minimal representation of a single TestCase in a TestFile. This
// object is _not_ reference type. It acts more like an index
//
// Rationale: This simplifies memory and object management with googletest
// parameterization which requires such objects to be constructable before
// the underlying test suite is constructible.
class TestCaseHandle {
 public:
  // Note, this string is guaranteed to be compatible with restrictions on
  // Googletest names and will contain only alphanumeric + underscore.
  template <typename Sink>
  friend void AbslStringify(Sink& sink, const TestCaseHandle& handle) {
    sink.Append(handle.name_);
  }

  // Needed for OSS (until we are able to upgrade to a more recent version of
  // googletest).
  friend void PrintTo(const TestCaseHandle& handle, std::ostream* os) {
    *os << absl::StrCat(handle);  // Use AbslStringify implementation
  }

 private:
  TestCaseHandle(int index, std::string name, bool skip_by_sharding)
      : index_(index), name_(name), skip_by_sharding_(skip_by_sharding) {}

  friend class TestFile;
  friend class TestFileRunner;
  friend class FileBasedTestDriverTestHelper;

  // Index into TestFile::raw_test_file::test_cases
  int index_;

  // This is a GoogleTest compatible name for the test.
  std::string name_;
  bool skip_by_sharding_;
};

// A simple immutable representation of a test case input.
//
// Note, alternations are not supported yet.
class TestCaseInput {
 public:
  // Name of the input file (as a relative path from the workspace root).
  absl::string_view filename() const { return filename_; }

  // The index of this test within it's file.
  int test_index() const { return test_index_; }

  // The line number of the start of the test (including comments).
  // This is zero for the first test, and corresponds to the first line
  // after the test-case separator ('==') for subsequent test cases.
  int start_line_number() const { return start_line_number_; }

  // Test case input text, excluding comments.
  absl::string_view text() const { return text_; }

 private:
  friend class TestFile;
  TestCaseInput(std::string filename, int test_index, int start_line_number,
                std::string text)
      : filename_(filename),
        test_index_(test_index),
        start_line_number_(start_line_number),
        text_(text) {}
  const std::string filename_;
  const int test_index_;
  const int start_line_number_;
  const std::string text_;
};

class TestFileRunner;

// A representation of test sharding that should be used in
// TestFile::ShardedTests.
//
// This will generally be derived from environment variables.
class ShardingEnvironment {
 public:
  static ShardingEnvironment FromEnv();

  bool is_sharded() const { return is_sharded_; }

  // If unsharded, returns 0;
  int this_shard() const { return this_shard_; }

  // If unsharded, returns 1.
  int total_shards() const { return total_shards_; }

 private:
  friend class FileBasedTestDriverTestHelper;
  bool is_sharded_ = false;
  int this_shard_ = 0;
  int total_shards_ = 1;
};

// Immutable representation of a File Based Test Driver input (.test) file.
//
// See TestFileRunner for usage.
class TestFile {
 public:
  TestFile(const TestFile& v);
  TestFile& operator=(const TestFile& v);
  TestFile(TestFile&& v);
  TestFile& operator=(TestFile&& v);

  ~TestFile();

  // Construct a TestFile from a file path by reading the file
  // and parsing it.
  static absl::StatusOr<TestFile> MakeFromFilepath(absl::string_view file_path);

  // Returns handles to the test in this file. Note, this
  // act more like indexes and do not contain references
  // to this object, and can freely be used on any copy of this
  // object.
  std::vector<TestCaseHandle> Tests() const;

  // Creates a vector of tests suitable for use with googletest parameterized
  // tests with sharding enabled.
  //
  //
  // `has_side_effects_fn` can be provided to indicate if a given test
  // has side_effects, in which case, it will be run on every shard.
  //
  // This is necessary, for instance, if the tests modifies default options.
  //
  // Alternations are not supported yet.
  absl::StatusOr<std::vector<TestCaseHandle>> ShardedTests(
      absl::FunctionRef<bool(const TestCaseInput&)> has_side_effects_fn,
      ShardingEnvironment sharding_environment =
          ShardingEnvironment::FromEnv());

  std::unique_ptr<TestFileRunner> MakeRunner(
      FileBasedTestDriverConfig config = {}) const;

  const std::string& filename() const;
  bool ContainsAlternations() const;

 private:
  TestFile() = default;
  friend class TestFileRunner;
  const internal::RawTestFile& raw_test_file() const { return *raw_test_file_; }
  std::unique_ptr<internal::RawTestFile> raw_test_file_;
};

// A stateful wrapper around a TestFile that can be used to actually execute
// tests. Uses RAII to initialize on construction and perform cleanup on
// destruction such as printing the updated golden file.
//
// Holds its own copy of TestFile to simplify object/memory management during
// googletest initialization.
//
// Usage:
//  {
//    FILE_BASED_TEST_DRIVER_ASSIGN_OR_RETURN(TestFile file, TestFile::MakeFromFilepath(...));
//    std::unique_ptr<TestFileRunner> runner = file.MakeRunner();
//    for (const TestCaseHandle& handle : runner->test_file().Tests()) {
//      runner.RunTestCase(handle,
//                         [](absl::string_view test_input,
//                            RunTestCaseResult* result) {
//                                result->RecordOutput(MyCode(test_input));
//                            });
//    }
//  }
//
//
// PARAMETERIZED TESTS
//
// googletest has a builtin api for running parameterized tests. The TestFile
// API supports this in the follow way (see also parameterized_example_test.cc):
//
//   class ExampleTest
//     : public ::testing::TestWithParam<file_based_test_driver::TestCaseHandle>
//     {
//
//     static void SetUpTestSuite() {
//       runner_ =
//       file_based_test_driver::RunnerForFile(TestFilePath()).release();
//     }
//     static void TearDownTestSuite() {
//       delete runner_;
//     }
//   };
//   file_based_test_driver::TestFileRunner* ExampleTest::runner_ = nullptr;
//
//
//   void Run(absl::string_view test_case,
//            file_based_test_driver::RunTestCaseResult* test_result) {...}
//
//   TEST_P(ExampleTest, RunTest) {
//     runner_->RunTestCase(GetParam(), &Run);
//   }
//
//   INSTANTIATE_TEST_SUITE_P(
//       ExampleTest, ExampleTest,
//       testing::ValuesIn(file_based_test_driver::TestsInFile(TestFilePath())),
//       testing::PrintToStringParamName());
//
// PARAMETERIZED TESTS WITH ALTERNATIONS
// Currently, all alternations are run as one googletest TestCase.
//
// BENEFITS OF PARAMETERIZATION
// A major upside to parameterized tests is that testing::Test::RecordProperty
// will correctly attach to individual test cases, rather than being global
// across the whole test (potentially clobbering each other).
//
// SHARDED PARAMETERIZED TESTS
//
// Supporting test sharding with parameterization is a little more complicated
// because tests are generally understood to be order dependent (see below)
// and likely to have side effects (such as options processing), a little more
// setup is required. In order to ensure correct computation, test cases with
// side effects need to be identified (including those setting default options).
// This is done via a callback when the test vector is constructed:
//
//   bool HasSideEffects(const TestCaseInput& input) {
//     return absl::StartsWith(input.text(), "[default");
//   }
//
//   INSTANTIATE_TEST_SUITE_P(
//       ExampleTest, ExampleTest,
//       testing::ValuesIn(file_based_test_driver::ShardedTestsInFile(
//           TestFilePath(), &HasSideEffects)),
//       testing::PrintToStringParamName());
//
// Tests with side effects are run on every shard.
//
// SHARDED PARAMETERIZED TESTS _WITH ALTERNATIONS_ ARE NOT SUPPORTED
//
// Sharded parameterized tests that contain alternations are not yet supported.
//
// ORDER DEPENDENT TESTS
//
// TestFileRunner requires tests to be run in file order (today) in order
// to produce the correct output for
//   --file_based_test_driver_generate_test_output
//
// Even if this is disabled, it's likely that tests are themselves order
// dependent due to options processing, or just being stateful.
//
// DETAILS ON HOW SHARDING WORKS
//
// Note, this refers to the current implemention. The details may change.
//
// Background on Googletest:
//
// When a test binary starts up, Googletest first determines the full list of
// tests it should run and ensures the names are valid and unique.
//
// Then Googletest examines environment variables to determine the requested
// sharding - the total number of shards and the shard assigned to this binary
// execution.
//
// Then it proceeds round robin through the list of tests in order, running
// every nth test starting from it's assigned shard.
//
// How file_based_test_driver sharding works.
//
// file_based_test_driver has to assume some side effects for a few reasons
// described above. Today it needs to ensure every test is 'run' (according to
// Googletest).
//
// To accomplish this, every TestCase in each file is repeated N times where
// N is the number of shards. This effectively undoes the round-robin that
// Googletest will issue while sharding.
//
// Then Googletest makes it's own determation of whether a test should actually
// be run. A test will be run if it has side effects (in which case it should)
// be run on all shards) or if Googletest _would have_ run it on this shard.
//
// This mostly should be invisible to the user, however the shards that
// file_based_test_driver skipped will show up with weird names in the list
// of 'skipped' tests in sponge.
//
class TestFileRunner {
 public:
  // Runs all tests associated with the given test_case.
  // `test_case_runner` will be invoked once per alternation.
  bool RunTestCase(
      TestCaseHandle test_case,
      absl::FunctionRef<void(absl::string_view test_input,
                             RunTestCaseResult* test_case_result /*out*/)>
          test_case_runner);

  // Runs all tests associated with the given test_case.
  bool RunTestCaseWithModes(
      TestCaseHandle test_case,
      absl::FunctionRef<
          void(absl::string_view test_input,
               RunTestCaseWithModesResult* test_case_result /* out */)>
          test_case_runner);

  // Return the underlying file object.
  const TestFile& test_file() const { return file_; }

  ~TestFileRunner();

 private:
  explicit TestFileRunner(TestFile file, FileBasedTestDriverConfig config);

  friend class TestFile;

  const internal::RawTestFile& raw_test_file() const;

  // We sidestep memory issues by just making a full copy.
  const TestFile file_;

  absl::StatusOr<absl::Nonnull<internal::RunTestCaseOutput*>> all_output();
  absl::StatusOr<absl::Nonnull<internal::RunTestCaseWithModesOutput*>>
  all_output_with_modes();

  // Exactly one of these will be set. Use unique_ptr to avoid making these
  // classes public.
  std::unique_ptr<internal::RunTestCaseOutput> all_output_;
  std::unique_ptr<internal::RunTestCaseWithModesOutput> all_output_with_modes_;
  const FileBasedTestDriverConfig config_;
};

// Note, this will die on errors on MakeFromFilePath.
inline std::vector<TestCaseHandle> TestsInFile(absl::string_view file_path) {
  return TestFile::MakeFromFilepath(file_path)->Tests();
}

inline std::vector<TestCaseHandle> ShardedTestsInFile(
    absl::string_view file_path,
    absl::FunctionRef<bool(const TestCaseInput&)> has_side_effects_fn) {
  return *TestFile::MakeFromFilepath(file_path)->ShardedTests(
      has_side_effects_fn);
}

// Note, this will die on errors on MakeFromFilePath.
inline std::unique_ptr<TestFileRunner> RunnerForFile(
    absl::string_view file_path) {
  return TestFile::MakeFromFilepath(file_path)->MakeRunner();
}

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
absl::Status GetNextTestCase(const std::vector<std::string>& lines,
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
// ("b,e", "abe"), and so on.  Inside alternations, \| can be used for a
// literal | character.  Other \ escape sequences pass through literally.
//
// Singleton alternations (e.g. {{a}}) are reported in singleton_alternations.
// It is useful when using the flag for disallowing such alternations,
// FLAGS_file_based_test_driver_disallow_singleton_alternation_groups.
void BreakStringIntoAlternations(
    absl::string_view input, const FileBasedTestDriverConfig& config,
    std::vector<std::pair<std::string, std::string>>*
        alternation_values_and_expanded_inputs,
    std::vector<std::string>& singleton_alternations);

}  // namespace internal
}  // namespace file_based_test_driver

#endif  // THIRD_PARTY_FILE_BASED_TEST_DRIVER_FILE_BASED_TEST_DRIVER_H_

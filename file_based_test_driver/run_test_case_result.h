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
#ifndef THIRD_PARTY_FILE_BASED_TEST_DRIVER_RUN_TEST_CASE_RESULT_H_
#define THIRD_PARTY_FILE_BASED_TEST_DRIVER_RUN_TEST_CASE_RESULT_H_

#include <string>
#include <vector>

#include "file_based_test_driver/test_case_outputs.h"

namespace file_based_test_driver {

// Base class for a RunTestCaseResult whether in 'WithModes' or in the default.
// This contains the read-only pieces of the RunTestCaseResult which are used
// to describe what input to the test is.
class RunTestCaseResultBase {
 public:
  RunTestCaseResultBase() = default;
  virtual ~RunTestCaseResultBase() = default;

  // RunTestCaseResultBase is neither copyable nor movable.
  RunTestCaseResultBase(const RunTestCaseResultBase&) = delete;
  RunTestCaseResultBase& operator=(const RunTestCaseResultBase&) = delete;

  bool ignore_test_output() const { return ignore_test_output_; }

  // If <value> is true, the test driver will assume that the test was
  // intentionally skipped. In this case, the test driver will pretend that the
  // test returned exactly the expected output. This defaults to false.
  //
  // WARNING: Take extra care when you use this option. If you inadvertently set
  // it for a test, it makes your test look like it passes.
  void set_ignore_test_output(bool value) { ignore_test_output_ = value; }

  // File name, line number, parts, and test_alternation of the current test. A
  // file-based test is separated into multiple parts by lines of "--". The
  // first part is the sql query with test case options. The rest parts are
  // expected results. Alternations can be found in a test with the use of
  // {{value1|value2}}, and multiple alternations can be found in a single test.
  // test_alternation contains the chosen values from the alternations for this
  // specific test in the order they appear in the test, joined on comma(",").
  const std::string& filename() const { return filename_; }
  int line() const { return line_; }
  const std::vector<std::string>& parts() const { return parts_; }
  const std::string& test_alternation() const { return test_alternation_; }

  // Setters for filename_, line_, parts_, and test_alternation_. These are
  // meant to be called by the file based test driver only. Should not be
  // called from individual test cases.
  // TODO: Move this to const members set at construction time.
  void set_filename(const std::string& filename) { filename_ = filename; }
  void set_line(int line) { line_ = line; }
  void set_parts(const std::vector<std::string>& parts) { parts_ = parts; }
  void set_test_alternation(const std::string& test_alternation) {
    test_alternation_ = test_alternation;
  }

  // Returns true if output has been added for the given test case.
  virtual bool IsEmpty() const = 0;

  // Firebolt Start
  bool expected_output_is_regex() const { return expected_output_is_regex_; }
  void set_expected_output_is_regex(bool expected_output_is_regex) {
    expected_output_is_regex_ = expected_output_is_regex;
  }

  bool compare_unsorted_result() const { return compare_unsorted_result_; }
  void set_compare_unsorted_result(bool compare_unsorted_result) {
    compare_unsorted_result_ = compare_unsorted_result;
  }
  // Firebolt End

 private:
  // If set to true, indicates that the test was intentionally skipped.
  // In this case, the test driver will pretend that the test returned exactly
  // the expected output.
  bool ignore_test_output_ = false;

  std::string filename_;
  int line_ = 0;
  std::vector<std::string> parts_;
  std::string test_alternation_;
  // Firebolt Start
  // If expected_output_is_regex_ is true, treat the expected output as a regex
  // that must match the entire actual output. Otherwise, the expected output
  // must match the actual output literally.
  bool expected_output_is_regex_{false};
  // If compare_unsorted_result_ is true, the actual result matches the expected
  // result even if the order of the lines is different. Currently, this is
  // achieved by lexicographically sorting the lines before comparison.
  bool compare_unsorted_result_{false};
  // Firebolt End
};

// The result of a test case run without Modes support. Output should be added
// through AddTestOutput with subsequent blocks of generated string output
// for the test.
class RunTestCaseResult : public RunTestCaseResultBase {
 public:
  RunTestCaseResult() {}
  ~RunTestCaseResult() override {}

  // RunTestCaseResult is neither copyable nor movable.
  RunTestCaseResult(const RunTestCaseResult&) = delete;
  RunTestCaseResult& operator=(const RunTestCaseResult&) = delete;

  // Adds <output> to the test outputs.
  void AddTestOutput(const std::string& output) {
    test_outputs_.push_back(output);
  }

  bool IsEmpty() const override { return test_outputs_.empty(); }

  const std::vector<std::string>& test_outputs() const { return test_outputs_; }
  std::vector<std::string>* mutable_test_outputs() { return &test_outputs_; }

 private:
  // The outputs of the test case.
  std::vector<std::string> test_outputs_;
};

// The result of a test case run without Modes support. Output should be added
// through mutable_test_case_outputs->RecordOutput. See the documentation for
// TestCaseOutputs for more complete description of 'Modes' support.
class RunTestCaseWithModesResult : public RunTestCaseResultBase {
 public:
  RunTestCaseWithModesResult() {}
  ~RunTestCaseWithModesResult() override {}

  // RunTestCaseWithModesResult is neither copyable nor movable.
  RunTestCaseWithModesResult(const RunTestCaseWithModesResult&) = delete;
  RunTestCaseWithModesResult& operator=(const RunTestCaseWithModesResult&) =
      delete;

  TestCaseOutputs* mutable_test_case_outputs() { return &test_case_outputs_; }

  const TestCaseOutputs& test_case_outputs() const {
    return test_case_outputs_;
  }

  bool IsEmpty() const override { return test_case_outputs_.IsEmpty(); }

 private:
  TestCaseOutputs test_case_outputs_;
};

}  // namespace file_based_test_driver

#endif  // THIRD_PARTY_FILE_BASED_TEST_DRIVER_RUN_TEST_CASE_RESULT_H_

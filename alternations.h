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
#ifndef THIRD_PARTY_FILE_BASED_TEST_DRIVER_ALTERNATIONS_H_
#define THIRD_PARTY_FILE_BASED_TEST_DRIVER_ALTERNATIONS_H_

#include <map>
#include <vector>

#include "run_test_case_result.h"
#include "absl/status/status.h"

namespace file_based_test_driver {

// Contains a collection of indiviudal alternation outputs as they are created
// and combines them into a single output.
class AlternationSet {
 public:
  AlternationSet() = default;

  // Records the output of a run of a single alternation named
  // 'alternation_name'.
  absl::Status Record(const std::string& alternation_name,
                      const RunTestCaseResult& test_case_result);

  // Processes all of the outputs accumulated from calls to 'Record'
  // and merges them into a single output in 'test_case_result'.
  absl::Status Finish(RunTestCaseResult* test_case_result);

 private:
  static const char kEmptyAlternationName[];
  bool finished_ = false;

  // This map hold {results -> [index into alternation_name_ that produced those
  // results]}. Indexes are stored to ensure consistent merged output by sorting
  // on the original alternation order.
  std::map<std::vector<std::string>, std::vector<int>> alternation_map_;
  std::vector<std::string> alternation_names_;
};

// Contains a collection of indiviudal alternation outputs as they are created
// and combines them into a single output for the 'WithModes' case.
class AlternationSetWithModes {
 public:
  AlternationSetWithModes() = default;

  // Records the output of a run of a single alternation named
  // 'alternation_name'.
  absl::Status Record(const std::string& alternation_name,
                      const RunTestCaseWithModesResult& test_case_result);

  // Processes all of the outputs accumulated from calls to 'Record'
  // and merges them into a single output in 'test_case_result'.
  absl::Status Finish(RunTestCaseWithModesResult* test_case_result);

 private:
  struct NameAndAlternationOutput {
    NameAndAlternationOutput(const std::string& name_in,
                             const TestCaseOutputs& outputs_in)
        : name(name_in), outputs(outputs_in) {}

    std::string name;
    TestCaseOutputs outputs;
  };

  // Mapping of 'output' to a list of alternation names.
  using OutputToAlternationNameMap =
      std::map<std::string, std::vector<std::string>>;

  // Mapping of 'result_type' to the collection of outputs.
  using ResultTypeToOutputMap =
      std::map<std::string, OutputToAlternationNameMap>;

  static const char kEmptyAlternationName[];

  // Returns in 'all_modes' the union of modes used by all of the alternations.
  // Sets PossibleModes in test_case_outputs from possible_modes in
  // alternations and returns an error if the possible_modes set in the
  // alternations differ.
  absl::Status CollectModes(TestCaseOutputs* test_case_outputs,
                            TestCaseMode::UnorderedSet* all_modes);

  // Adds output to 'test_case_outputs' for the mode 'mode' from the
  // alternations.
  absl::Status BuildSingleMode(const TestCaseMode& mode,
                               TestCaseOutputs* test_case_outputs);

  // Creates a mapping in 'result_type_to_output_map' of 'result_type' to test
  // output for all of the alternations which were run in mode 'mode'.
  absl::Status CollectAlternations(
      const TestCaseMode& mode,
      ResultTypeToOutputMap* result_type_to_output_map);

  // Adds the output for 'mode' and 'result_type' without annotating
  // alternations if all of the alternations produced identical output.
  // Returns true in 'added' if this was the case.
  absl::Status MaybeAddSingleOutput(
      const TestCaseMode& mode, const std::string& result_type,
      const OutputToAlternationNameMap& output_map,
      TestCaseOutputs* test_case_outputs, bool* added);

  // Adds outputs to 'test_case_outputs' for each distinct alternation output
  // with the 'result_type' annotated to include which alternations generated
  // that output.
  absl::Status CombineAlternations(const TestCaseMode& mode,
                                   const std::string& result_type,
                                   const OutputToAlternationNameMap& output_map,
                                   TestCaseOutputs* test_case_outputs);

  // Has Finish been called.
  bool finished_ = false;

  // Vector to preserve the order of input when grouping alternations together.
  std::vector<NameAndAlternationOutput> alternations_;
};

}  // namespace file_based_test_driver

#endif  // THIRD_PARTY_FILE_BASED_TEST_DRIVER_ALTERNATIONS_H_

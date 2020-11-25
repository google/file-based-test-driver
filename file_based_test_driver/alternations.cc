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
#include "file_based_test_driver/alternations.h"

#include "absl/strings/str_join.h"
#include "re2/re2.h"
#include "file_based_test_driver/base/ret_check.h"

namespace file_based_test_driver {

const char AlternationSet::kEmptyAlternationName[] = "<empty>";
const char AlternationSetWithModes::kEmptyAlternationName[] = "EMPTY";

absl::Status AlternationSet::Record(const std::string& alternation_name,
                                    const RunTestCaseResult& test_case_result) {
  FILE_BASED_TEST_DRIVER_RET_CHECK(!finished_);
  alternation_map_[test_case_result.test_outputs()].push_back(
      alternation_names_.size());
  alternation_names_.push_back(alternation_name.empty() ? kEmptyAlternationName
                                                        : alternation_name);
  return absl::OkStatus();
}

absl::Status AlternationSet::Finish(RunTestCaseResult* test_case_result) {
  FILE_BASED_TEST_DRIVER_RET_CHECK(!finished_);
  finished_ = true;
  std::vector<std::string>* test_outputs =
      test_case_result->mutable_test_outputs();
  FILE_BASED_TEST_DRIVER_RET_CHECK(test_outputs->empty());

  // If all of the results are the same, leave output as is. Otherwise,
  // process output into the form:
  //   {{{<semicolon separated list of alternation groups}}}
  //   <results for these groups>
  //   ----
  //   {{{<next groups>}}}
  //   <results for these groups>
  //   ...
  //
  // We'll group all alternations with equal outputs together,
  // ordering alternations by their original generation order.
  if (alternation_map_.size() <= 1) {
    if (!alternation_map_.empty()) {
      *test_outputs = alternation_map_.begin()->first;
    }
  } else {
    // Map the sorted vector of input indices with the same output
    // to that output value.
    // This will also sort the map by the first element of each vector.
    std::map<std::vector<int>, std::vector<std::string>> transposed_results_map;

    // Transpose alternation_map_ so it maps from a vector of indices (sorted)
    // to their common test output.
    // The vector of indices is already sorted, by construction.
    for (const auto& it : alternation_map_) {
      transposed_results_map[it.second] = it.first;
    }

    for (const auto& it : transposed_results_map) {
      const std::vector<int>& alternation_index_list = it.first;
      const std::vector<std::string>& group_output = it.second;

      std::vector<std::string> match_groups;
      match_groups.reserve(alternation_index_list.size());
      for (int idx : alternation_index_list) {
        match_groups.push_back(alternation_names_[idx]);
      }

      if (match_groups.size() > 1) {
        match_groups.insert(match_groups.begin(), "ALTERNATION GROUPS:");
        const std::string groups_list = absl::StrJoin(match_groups, "\n    ");
        test_outputs->push_back(groups_list);
      } else {
        test_outputs->push_back(
            absl::StrCat("ALTERNATION GROUP: ", match_groups[0]));
      }
      test_outputs->insert(test_outputs->end(), group_output.begin(),
                           group_output.end());
    }
  }

  test_outputs->insert(test_outputs->begin(), test_case_result->parts()[0]);
  alternation_map_.clear();

  return absl::OkStatus();
}

absl::Status AlternationSetWithModes::Record(
    const std::string& alternation_name,
    const RunTestCaseWithModesResult& test_case_result) {
  FILE_BASED_TEST_DRIVER_RET_CHECK(!finished_);
  static LazyRE2 re = {"[\\n\\{\\}\\<\\>]"};
  FILE_BASED_TEST_DRIVER_RET_CHECK(!RE2::PartialMatch(alternation_name, *re))
      << "Alternation \"" << alternation_name << "\" contains names that can't "
      << "be stored in a result_type: " << re->pattern();
  alternations_.emplace_back(
      alternation_name.empty() ? kEmptyAlternationName : alternation_name,
      test_case_result.test_case_outputs());
  return absl::OkStatus();
}

absl::Status AlternationSetWithModes::Finish(
    RunTestCaseWithModesResult* test_case_result) {
  FILE_BASED_TEST_DRIVER_RET_CHECK(!finished_);
  finished_ = true;

  TestCaseOutputs* test_case_outputs =
      test_case_result->mutable_test_case_outputs();

  TestCaseMode::UnorderedSet all_modes;
  FILE_BASED_TEST_DRIVER_RETURN_IF_ERROR(CollectModes(test_case_outputs, &all_modes));

  for (const TestCaseMode& mode : all_modes) {
    FILE_BASED_TEST_DRIVER_RETURN_IF_ERROR(BuildSingleMode(mode, test_case_outputs));
  }

  return absl::OkStatus();
}

absl::Status AlternationSetWithModes::CollectModes(
    TestCaseOutputs* test_case_outputs, TestCaseMode::UnorderedSet* all_modes) {
  bool set_possible_modes = true;
  for (const NameAndAlternationOutput& alternation : alternations_) {
    if (set_possible_modes) {
      FILE_BASED_TEST_DRIVER_RETURN_IF_ERROR(test_case_outputs->SetPossibleModes(
          alternation.outputs.possible_modes()));
      set_possible_modes = false;
    } else {
      FILE_BASED_TEST_DRIVER_RET_CHECK(test_case_outputs->possible_modes() ==
                alternation.outputs.possible_modes())
          << "Different possible modes for differerent alternations are not "
          << "allowed: {"
          << absl::StrJoin(test_case_outputs->possible_modes(), ",",
                           TestCaseMode::JoinFormatter())
          << "} vs {"
          << absl::StrJoin(alternation.outputs.possible_modes(), ",",
                           TestCaseMode::JoinFormatter())
          << "}";
    }
    for (const auto& mode_to_results : alternation.outputs.outputs_) {
      const TestCaseMode& mode = mode_to_results.first;
      all_modes->insert(mode);
    }
  }

  return absl::OkStatus();
}

absl::Status AlternationSetWithModes::BuildSingleMode(
    const TestCaseMode& mode, TestCaseOutputs* test_case_outputs) {
  ResultTypeToOutputMap result_type_to_output_map;

  FILE_BASED_TEST_DRIVER_RETURN_IF_ERROR(CollectAlternations(mode, &result_type_to_output_map));

  for (const auto& pair : result_type_to_output_map) {
    const std::string& result_type = pair.first;
    const OutputToAlternationNameMap& output_map = pair.second;
    bool added;
    FILE_BASED_TEST_DRIVER_RETURN_IF_ERROR(MaybeAddSingleOutput(mode, result_type, output_map,
                                         test_case_outputs, &added));
    if (!added) {
      FILE_BASED_TEST_DRIVER_RETURN_IF_ERROR(CombineAlternations(mode, result_type, output_map,
                                          test_case_outputs));
    }
  }

  return absl::OkStatus();
}

absl::Status AlternationSetWithModes::CollectAlternations(
    const TestCaseMode& mode,
    ResultTypeToOutputMap* result_type_to_output_map) {
  for (const NameAndAlternationOutput& alternation : alternations_) {
    auto it = alternation.outputs.outputs_.find(mode);
    FILE_BASED_TEST_DRIVER_RET_CHECK(it != alternation.outputs.outputs_.end());
    const TestCaseOutputs::ModeResults& mode_results = it->second;

    for (const auto& pair : mode_results) {
      const std::string& result_type = pair.first;
      const std::string& output = pair.second;
      (*result_type_to_output_map)[result_type][output].push_back(
          alternation.name);
    }
  }
  return absl::OkStatus();
}

absl::Status AlternationSetWithModes::MaybeAddSingleOutput(
    const TestCaseMode& mode, const std::string& result_type,
    const OutputToAlternationNameMap& output_map,
    TestCaseOutputs* test_case_outputs, bool* added) {
  *added = false;
  if (output_map.size() != 1) {
    return absl::OkStatus();
  }

  const std::pair<std::string, std::vector<std::string>>& output_and_idx_list =
      *output_map.begin();
  if (output_and_idx_list.second.size() != alternations_.size()) {
    return absl::OkStatus();
  }

  FILE_BASED_TEST_DRIVER_RETURN_IF_ERROR(test_case_outputs->RecordOutput(mode, result_type,
                                                  output_and_idx_list.first));
  *added = true;

  return absl::OkStatus();
}

absl::Status AlternationSetWithModes::CombineAlternations(
    const TestCaseMode& mode, const std::string& result_type,
    const OutputToAlternationNameMap& output_map,
    TestCaseOutputs* test_case_outputs) {
  for (const auto& output_and_names : output_map) {
    const std::string& output = output_and_names.first;
    const std::vector<std::string>& alternation_names = output_and_names.second;

    const std::string annotated_result_type = absl::StrCat(
        result_type, "{", absl::StrJoin(alternation_names, "}{"), "}");

    FILE_BASED_TEST_DRIVER_RETURN_IF_ERROR(
        test_case_outputs->RecordOutput(mode, annotated_result_type, output));
  }
  return absl::OkStatus();
}

}  // namespace file_based_test_driver

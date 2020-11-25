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
#include "file_based_test_driver/test_case_outputs.h"

#include <algorithm>
#include <initializer_list>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/node_hash_map.h"
#include "absl/status/status.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "file_based_test_driver/test_case_mode.h"
#include "file_based_test_driver/base/map_util.h"
#include "re2/re2.h"
#include "file_based_test_driver/base/ret_check.h"
#include "file_based_test_driver/base/status_macros.h"

namespace file_based_test_driver {
namespace {

constexpr absl::string_view kPossibleModesPrefix = "Possible Modes:";

// Return structure from ParseFirstLine.
struct FirstLineParseResult {
  // Points into the parse string for the first_line that was parsed.
  absl::string_view first_line;
  // Points into parsed string corresponding to everything after 'first_line',
  // or if 'first_line' is empty, the entire buffer.
  absl::string_view remainder;
  // If true, the first line corresponded to a 'Possible Modes:' entry.
  bool is_possible_modes;
  // The parsed result type.
  std::string result_type;
  // The modes specified either by possible modes or by the result type.
  std::vector<TestCaseMode> test_modes;
};

// Pulls the first line from 'part' and fills in 'result' according to the data
// within it.
// If the line starts with 'Possible Modes:', this line contains all the
// possible test modes and 'is_possible_modes' is set to true.
// The possible patterns are:
// 'Possible Modes: [test_mode1][test_mode2]...'
// '<result type>[test mode1][test mode2]...'
// '<>[test mode1][test mode2]...'
// '<result type>'
// Alternations are included in the 'result type' so are not handled separately
// parsing back.
absl::Status ParseFirstLine(const absl::string_view part,
                            FirstLineParseResult* result) {
  FILE_BASED_TEST_DRIVER_RET_CHECK(result != nullptr);

  *result = FirstLineParseResult();
  result->test_modes.clear();
  result->remainder = absl::string_view(part);

  const absl::string_view::size_type index =
      result->remainder.find_first_of('\n');
  if (index != absl::string_view::npos) {
    result->first_line = result->remainder.substr(0, index);
  } else {
    result->first_line = result->remainder;
  }

  absl::string_view stripped_first_line = result->first_line;
  stripped_first_line = absl::StripLeadingAsciiWhitespace(stripped_first_line);
  absl::string_view test_modes_sp;
  if (absl::ConsumePrefix(&stripped_first_line, kPossibleModesPrefix)) {
    result->is_possible_modes = true;
    test_modes_sp = stripped_first_line;
  } else {
    result->is_possible_modes = false;
    // TODO: Support [\n{}<>] in alternation modes with escaping.
    if (!RE2::FullMatch(stripped_first_line, "^<([^>]*)>(.*)",
                        &result->result_type, &test_modes_sp)) {
      return absl::OkStatus();
    }
  }

  absl::ConsumePrefix(&result->remainder, result->first_line);
  absl::ConsumePrefix(&result->remainder, "\n");

  FILE_BASED_TEST_DRIVER_ASSIGN_OR_RETURN(result->test_modes, TestCaseMode::ParseModes(test_modes_sp),
                   _.SetAppend() << result->first_line);
  return absl::OkStatus();
}

}  // namespace

absl::Status TestCaseOutputs::RecordOutput(const TestCaseMode& test_mode,
                                           absl::string_view result_type,
                                           absl::string_view output) {
  FILE_BASED_TEST_DRIVER_RET_CHECK(!test_mode.empty());
  std::string output_with_newline = std::string(output);
  // Ensure all outputs end in '\n'.
  if (!output.empty() && !absl::EndsWith(output, "\n")) {
    absl::StrAppend(&output_with_newline, "\n");
  }
  return AddOutputInternal(test_mode, result_type, output_with_newline);
}

absl::Status TestCaseOutputs::AddOutputInternal(const TestCaseMode& test_mode,
                                                absl::string_view result_type,
                                                absl::string_view output) {
  ModeResults& mode_results =
      file_based_test_driver_base::LookupOrInsert(&outputs_, test_mode, ModeResults());
  std::string found_output;
  if (mode_results.GetOutputForResultType(result_type, &found_output)) {
    return ::file_based_test_driver_base::UnknownErrorBuilder().LogError()
           << "An output already exists for mode '" << test_mode
           << "', result type '" << result_type << "':\n"
           << "first output:\n"
           << found_output << "\nsecond output:\n"
           << output;
  }
  if (!test_mode.empty()) {
    if (!possible_modes_.empty() &&
        !file_based_test_driver_base::ContainsKey(possible_modes_, test_mode)) {
      return ::file_based_test_driver_base::UnknownErrorBuilder().LogError()
             << "Cannot add output:\n"
             << output << "\nfor mode '" << test_mode << "' and result type '"
             << result_type << "'\nbecause mode '" << test_mode
             << "' does not exist in the possible modes list: '"
             << absl::StrJoin(possible_modes_, ",",
                              TestCaseMode::JoinFormatter())
             << "'.";
    }
    ModeResults* all_modes_outputs = file_based_test_driver_base::FindOrNull(outputs_, TestCaseMode());
    if (all_modes_outputs != nullptr) {
      if (all_modes_outputs->GetOutputForResultType(result_type,
                                                    &found_output)) {
        return ::file_based_test_driver_base::UnknownErrorBuilder().LogError()
               << "Cannot add output for mode '" << test_mode
               << "' and result type '" << result_type
               << "' because an 'all modes' output exists for the "
               << "result type:\nall modes output:\n"
               << found_output;
      }
    }
  } else {
    for (const auto& item : outputs_) {
      const TestCaseMode& mode = item.first;
      const ModeResults& mode_results = item.second;

      if (mode.empty()) continue;
      if (mode_results.GetOutputForResultType(result_type, &found_output)) {
        return ::file_based_test_driver_base::UnknownErrorBuilder().LogError()
               << "Cannot add all modes output for result type '" << result_type
               << "' because a '" << mode
               << "' output already exists for the result type\n"
               << "modes specific output:\n"
               << found_output;
        }
    }
  }
  FILE_BASED_TEST_DRIVER_RET_CHECK(mode_results.AddOutput(result_type, output));
  return absl::OkStatus();
}

absl::Status TestCaseOutputs::GetCombinedOutputs(
    bool include_possible_modes,
    std::vector<std::string>* combined_outputs) const {
  typedef std::map<std::string, TestCaseMode::Set> OutputToModesMap;
  typedef std::map<std::string, OutputToModesMap> ResultTypeOutputModesMap;
  ResultTypeOutputModesMap result_type_to_output_modes_map;

  // If several test modes have the same output for the same result type,
  // combine them together.
  // It generates 'result_type_to_output_modes_map'
  // ([result_type -> [output -> test_modes]]) from 'output_'
  // ([test_mode -> [result_type -> output]])
  for (const auto& item : outputs_) {
    const TestCaseMode& test_mode = item.first;
    const ModeResults& mode_results = item.second;

    for (const auto& result_and_output : mode_results) {
      const std::string& result_type = result_and_output.first;
      const std::string& output = result_and_output.second;
      OutputToModesMap& output_to_modes_map = file_based_test_driver_base::LookupOrInsert(
          &result_type_to_output_modes_map, result_type, OutputToModesMap());
      TestCaseMode::Set& modes = file_based_test_driver_base::LookupOrInsert(
          &output_to_modes_map, output, TestCaseMode::Set());
      file_based_test_driver_base::InsertIfNotPresent(&modes, test_mode);
    }
  }

  // Add possible modes in the output
  if (include_possible_modes && !possible_modes_.empty()) {
    combined_outputs->push_back(absl::StrCat(
        kPossibleModesPrefix, " [",
        absl::StrJoin(possible_modes_, "][", TestCaseMode::JoinFormatter()),
        "]\n"));
  }
  // Generates the final outputs.
  for (const auto& item : result_type_to_output_modes_map) {
    std::vector<std::string> outputs_for_result_type;
    const std::string& result_type = item.first;
    const OutputToModesMap& output_modes_map = item.second;
    for (const auto& output_modes : output_modes_map) {
      const std::string& output = output_modes.first;
      const TestCaseMode::Set& modes = output_modes.second;
      FILE_BASED_TEST_DRIVER_RET_CHECK(!modes.empty());
      // Construct the output string.
      std::string output_str;
      if (!result_type.empty() || !modes.begin()->empty()) {
        absl::StrAppend(&output_str, "<", result_type, ">");
      }
      absl::StrAppend(&output_str, TestCaseMode::CollapseModes(modes));
      if (!output_str.empty()) absl::StrAppend(&output_str, "\n");
      absl::StrAppend(&output_str, output);
      outputs_for_result_type.push_back(output_str);
    }
    std::sort(outputs_for_result_type.begin(), outputs_for_result_type.end());
    for (const std::string& output_str : outputs_for_result_type) {
      combined_outputs->push_back(output_str);
    }
  }
  return absl::OkStatus();
}

absl::Status TestCaseOutputs::ParseFrom(const std::vector<std::string>& parts) {
  for (const std::string& part : parts) {
    FirstLineParseResult parse_result;
    FILE_BASED_TEST_DRIVER_RETURN_IF_ERROR(ParseFirstLine(part, &parse_result));
    if (parse_result.is_possible_modes) {
      possible_modes_.insert(parse_result.test_modes.begin(),
                             parse_result.test_modes.end());
      continue;
    }
    const std::string output = std::string(parse_result.remainder);
    if (parse_result.test_modes.empty()) {
      FILE_BASED_TEST_DRIVER_RETURN_IF_ERROR(
          AddOutputInternal(TestCaseMode(), parse_result.result_type, output))
          << part;
    } else {
      for (const TestCaseMode& test_mode : parse_result.test_modes) {
        FILE_BASED_TEST_DRIVER_RETURN_IF_ERROR(
            AddOutputInternal(test_mode, parse_result.result_type, output))
            << part;
      }
    }
  }
  return absl::OkStatus();
}

TestCaseMode::UnorderedSet TestCaseOutputs::GetTestModes() const {
  TestCaseMode::UnorderedSet test_modes(possible_modes_.begin(),
                                        possible_modes_.end());
  if (!test_modes.empty()) return test_modes;
  for (const auto& item : outputs_) {
    if (!item.first.empty()) test_modes.insert(item.first);
  }
  return test_modes;
}

bool TestCaseOutputs::HasAllModesResult() const {
  return file_based_test_driver_base::ContainsKey(outputs_, TestCaseMode());
}

absl::Status TestCaseOutputs::BreakOutAllModesOutputs(
    const TestCaseMode::UnorderedSet& test_modes) {
  // Remove the 'all modes' outputs from 'outputs_' and replace them with mode
  // specific outputs.
  const ModeResults* all_modes_output =
      file_based_test_driver_base::FindOrNull(outputs_, TestCaseMode());
  if (all_modes_output != nullptr) {
    for (const auto& result_and_output : *all_modes_output) {
      const std::string& result_type = result_and_output.first;
      const std::string& output = result_and_output.second;

      for (const TestCaseMode& test_mode : test_modes) {
        ModeResults& result_type_to_output_map =
            file_based_test_driver_base::LookupOrInsert(&outputs_, test_mode, ModeResults());
        FILE_BASED_TEST_DRIVER_RET_CHECK(result_type_to_output_map.AddOutput(result_type, output));
      }
    }
    FILE_BASED_TEST_DRIVER_RET_CHECK_EQ(size_t{1}, outputs_.erase(TestCaseMode()));
  }
  return absl::OkStatus();
}

absl::Status TestCaseOutputs::GenerateAllModesOutputs(
    const TestCaseMode::UnorderedSet& test_modes) {
  // If an output appears in all test modes, remove the mode specific outputs
  // and add a 'all mode' output.
  typedef absl::node_hash_map<std::string, TestCaseMode::UnorderedSet>
      OutputToModesMap;
  typedef absl::node_hash_map<std::string, OutputToModesMap>
      ResultTypeToOutputToModesMap;
  ResultTypeToOutputToModesMap result_type_to_output_modes_map;

  for (const auto& item : outputs_) {
    const TestCaseMode& test_mode = item.first;
    FILE_BASED_TEST_DRIVER_RET_CHECK(file_based_test_driver_base::ContainsKey(test_modes, test_mode));
    const ModeResults& mode_results = item.second;

    for (const auto& result_and_output : mode_results) {
      const std::string& result_type = result_and_output.first;
      const std::string& output = result_and_output.second;

      // Create the map entries if not present:
      TestCaseMode::UnorderedSet& modes =
          result_type_to_output_modes_map[result_type][output];
      file_based_test_driver_base::InsertIfNotPresent(&modes, test_mode);
    }
  }

  for (const auto& item : result_type_to_output_modes_map) {
    const std::string& result_type = item.first;
    const OutputToModesMap& output_to_modes_map = item.second;
    for (const auto& output_modes : output_to_modes_map) {
      const std::string& output = output_modes.first;
      const TestCaseMode::UnorderedSet& modes = output_modes.second;
      if (modes == test_modes) {
        ModeResults& result_type_to_output_map =
            file_based_test_driver_base::LookupOrInsert(&outputs_, TestCaseMode(), ModeResults());
        FILE_BASED_TEST_DRIVER_RET_CHECK(result_type_to_output_map.AddOutput(result_type, output));
        for (const TestCaseMode& mode : test_modes) {
          ModeResults* result_type_to_output_map =
              file_based_test_driver_base::FindOrNull(outputs_, mode);
          FILE_BASED_TEST_DRIVER_RET_CHECK(result_type_to_output_map != nullptr);
          FILE_BASED_TEST_DRIVER_RET_CHECK(result_type_to_output_map->RemoveResultType(result_type));
          if (result_type_to_output_map->empty()) {
            FILE_BASED_TEST_DRIVER_RET_CHECK_EQ(size_t{1}, outputs_.erase(mode));
          }
        }
      }
    }
  }
  return absl::OkStatus();
}

absl::Status TestCaseOutputs::InsertOrUpdateOutputsForTestModes(
    const TestCaseOutputs& outputs,
    const TestCaseMode::UnorderedSet& test_modes) {
  for (const auto& mode_output_map : outputs.outputs_) {
    if (file_based_test_driver_base::ContainsKey(test_modes, mode_output_map.first)) {
      file_based_test_driver_base::InsertOrUpdate(&outputs_, mode_output_map);
    }
  }
  return absl::OkStatus();
}

absl::Status TestCaseOutputs::DisableTestMode(
    const TestCaseMode& disabled_mode) {
  disabled_modes_.insert(disabled_mode);
  if (file_based_test_driver_base::ContainsKey(outputs_, disabled_mode)) {
    FILE_BASED_TEST_DRIVER_RET_CHECK_EQ(size_t{1}, outputs_.erase(disabled_mode));
  }
  return absl::OkStatus();
}

absl::Status TestCaseOutputs::ValidatePossibleModes() const {
  for (const auto& item : outputs_) {
    if (!item.first.empty() && !file_based_test_driver_base::ContainsKey(possible_modes_, item.first)) {
      return ::file_based_test_driver_base::UnknownErrorBuilder().LogError()
             << "Cannot set possible modes to '"
             << absl::StrJoin(possible_modes_, ",",
                              TestCaseMode::JoinFormatter())
             << "' because mode '" << item.first << "' exists in the "
             << "actual output but does not exist in the possible modes.";
    }
  }
  return absl::OkStatus();
}

absl::Status TestCaseOutputs::SetPossibleModes(
    const std::initializer_list<TestCaseMode>& possible_modes) {
  possible_modes_.clear();
  if (possible_modes.size() == 0) return absl::OkStatus();
  for (const TestCaseMode& mode : possible_modes) {
    FILE_BASED_TEST_DRIVER_RET_CHECK(!mode.empty());
    possible_modes_.insert(mode);
  }
  FILE_BASED_TEST_DRIVER_RETURN_IF_ERROR(ValidatePossibleModes());
  return absl::OkStatus();
}

absl::Status TestCaseOutputs::SetPossibleModes(
    TestCaseMode::Set possible_modes) {
  possible_modes_ = std::move(possible_modes);
  if (possible_modes_.empty()) return absl::OkStatus();
  for (const TestCaseMode& mode : possible_modes_) {
    FILE_BASED_TEST_DRIVER_RET_CHECK(!mode.empty());
  }
  FILE_BASED_TEST_DRIVER_RETURN_IF_ERROR(ValidatePossibleModes());
  return absl::OkStatus();
}

// static
absl::Status TestCaseOutputs::MergeOutputs(
    const TestCaseOutputs& expected_outputs,
    const std::vector<TestCaseOutputs>& actual_outputs,
    TestCaseOutputs* merged_outputs) {
  FILE_BASED_TEST_DRIVER_RET_CHECK(merged_outputs != nullptr);
  // Collect the possible modes from actual_outputs and make sure they are same.
  TestCaseMode::Set possible_modes;
  size_t possible_modes_idx;
  for (size_t i = 0; i < actual_outputs.size(); ++i) {
    const TestCaseOutputs& outputs = actual_outputs[i];
    if (!outputs.possible_modes().empty()) {
      if (possible_modes.empty()) {
        possible_modes = outputs.possible_modes();
        possible_modes_idx = i;
      } else {
        if (possible_modes != outputs.possible_modes()) {
          std::vector<std::string> combined_outputs1;
          FILE_BASED_TEST_DRIVER_RET_CHECK_OK(
              actual_outputs[possible_modes_idx].GetCombinedOutputs(
                  true /* include_possible_modes*/, &combined_outputs1));
          std::vector<std::string> combined_outputs2;
          FILE_BASED_TEST_DRIVER_RET_CHECK_OK(outputs.GetCombinedOutputs(
              true /* include_possible_modes*/, &combined_outputs2));
          return ::file_based_test_driver_base::UnknownErrorBuilder().LogError()
                 << "Cannot merge the following two outputs because their "
                 << "possible modes lists are different:"
                 << "\nFirst possible modes:\n"
                 << absl::StrJoin(possible_modes, ", ",
                                  TestCaseMode::JoinFormatter())
                 << "\nSecond possible modes:\n"
                 << absl::StrJoin(outputs.possible_modes(), ", ",
                                  TestCaseMode::JoinFormatter())
                 << "\nFirst outputs:\n"
                 << absl::StrJoin(combined_outputs1, "--\n")
                 << "\nSecond outputs:\n"
                 << absl::StrJoin(combined_outputs2, "--\n");
        }
      }
    }
  }
  // Collect all the test modes.
  TestCaseMode::UnorderedSet test_modes = expected_outputs.GetTestModes();
  TestCaseMode::UnorderedSet disabled_modes;
  bool has_actual_output = false;
  for (const TestCaseOutputs& outputs : actual_outputs) {
    // Actual outputs should not have 'all modes' outputs.
    if (outputs.HasAllModesResult()) {
      std::vector<std::string> combined_outputs;
      FILE_BASED_TEST_DRIVER_RET_CHECK_OK(outputs.GetCombinedOutputs(false /* include_possible_modes*/,
                                              &combined_outputs));
      return ::file_based_test_driver_base::UnknownErrorBuilder().LogError()
             << "Cannot merge partition output because it contains "
             << "'all modes' result:\n"
             << absl::StrJoin(combined_outputs, "\n--\n");
    }
    const TestCaseMode::UnorderedSet outputs_test_modes =
        outputs.GetTestModes();
    test_modes.insert(outputs_test_modes.begin(), outputs_test_modes.end());
    disabled_modes.insert(outputs.disabled_modes().begin(),
                          outputs.disabled_modes().end());
    if (!outputs.outputs_.empty()) has_actual_output = true;
  }

  // Remove all the test modes that are not in the possible modes list if any
  // of the actual outputs has 'possible_modes_' set.
  if (!possible_modes.empty()) {
    for (const TestCaseMode& mode : test_modes) {
      if (!file_based_test_driver_base::ContainsKey(possible_modes, mode)) {
        disabled_modes.insert(mode);
      }
    }
  }

  for (const TestCaseMode& disabled_mode : disabled_modes) {
    test_modes.erase(disabled_mode);
  }

  *merged_outputs = expected_outputs;
  for (const TestCaseMode& disabled_mode : disabled_modes) {
    FILE_BASED_TEST_DRIVER_RETURN_IF_ERROR(merged_outputs->DisableTestMode(disabled_mode));
  }

  // If the actual output is empty, we will keep the existing output.
  // Otherwise it may delete 'all modes' output if it's the only output left.
  if (!has_actual_output) {
    return absl::OkStatus();
  }

  FILE_BASED_TEST_DRIVER_RETURN_IF_ERROR(merged_outputs->BreakOutAllModesOutputs(test_modes));

  for (const TestCaseOutputs& outputs : actual_outputs) {
    FILE_BASED_TEST_DRIVER_RETURN_IF_ERROR(
        merged_outputs->InsertOrUpdateOutputsForTestModes(outputs, test_modes));
  }

  FILE_BASED_TEST_DRIVER_RETURN_IF_ERROR(merged_outputs->GenerateAllModesOutputs(test_modes));
  return absl::OkStatus();
}

}  // namespace file_based_test_driver

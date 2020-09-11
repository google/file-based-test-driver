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
#include "test_case_options.h"

#include <iterator>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "base/source_location.h"
#include "base/ret_check.h"
#include "base/status_builder.h"
#include "base/status_macros.h"

namespace file_based_test_driver {

absl::Status TestCaseOptions::ParseTestCaseOptions(std::string* str,
                                                   bool allow_defaults,
                                                   bool* defaults_found) {
  if (defaults_found != nullptr) {
    *defaults_found = false;
  }

  // Parse all options of the form [...].
  std::vector<std::string> option_strings;
  FILE_BASED_TEST_DRIVER_RETURN_IF_ERROR(ExtractAndRemoveOptionStrings(str, &option_strings));

  // Split the options into [default ...] and the rest.
  std::vector<std::string> default_option_strings;
  std::vector<std::string> nondefault_option_strings;
  for (const std::string& option_str : option_strings) {
    if (absl::StartsWithIgnoreCase(option_str, "default ")) {
      if (!allow_defaults) {
        return ::file_based_test_driver_base::UnknownErrorBuilder().LogError()
               << "default option \"" << option_str << "\" specified when "
               << "defaults are not allowed";
      }
      if (defaults_found != nullptr) {
        *defaults_found = true;
      }
      default_option_strings.emplace_back(option_str.substr(8));
    } else {
      nondefault_option_strings.emplace_back(std::move(option_str));
    }
  }

  // Set default values from the [default ...] options first.
  FILE_BASED_TEST_DRIVER_RETURN_IF_ERROR(SetOptionValuesFromStrings(default_option_strings,
                                             /*set_default=*/true));

  // Reset actual values for all options to their default values.
  for (auto& options_element : options_by_lower_keyword_) {
    TestCaseOption& option = options_element.second;
    option.current_value = option.default_value;
    DCHECK(!option.current_value.is_set_explicitly);
  }

  // Finally, set the actual values from the actual (non-"default") options.
  FILE_BASED_TEST_DRIVER_RETURN_IF_ERROR(SetOptionValuesFromStrings(nondefault_option_strings,
                                             /*set_default=*/false));
  return absl::OkStatus();
}

absl::Status TestCaseOptions::ExtractAndRemoveOptionStrings(
    std::string* str, std::vector<std::string>* option_strings) {
  absl::StripLeadingAsciiWhitespace(str);
  while (absl::StartsWith(*str, "[")) {
    // Scan and match [ and ].
    int unmatched_left_square_brackets = 0;
    std::string::iterator iter;
    for (iter = str->begin(); iter < str->end(); ++iter) {
      unmatched_left_square_brackets += ('[' == *iter);
      unmatched_left_square_brackets -= (']' == *iter);
      if (!unmatched_left_square_brackets) break;
    }
    FILE_BASED_TEST_DRIVER_RET_CHECK(iter < str->end())
        << "Unclosed [ while processing TestCaseOptions for string:\n" << *str;
    const std::string::size_type endpos = std::distance(str->begin(), iter);
    std::string option_str = str->substr(1, endpos - 1);
    str->erase(0, endpos + 1);
    absl::StripLeadingAsciiWhitespace(str);

    option_strings->emplace_back(std::move(option_str));
  }
  return absl::OkStatus();
}

absl::Status TestCaseOptions::SetOptionValuesFromStrings(
    const std::vector<std::string>& option_strings, bool set_default) {
  for (const std::string& option_str : option_strings) {
    const std::string::size_type equal_pos = option_str.find('=');
    std::string value;
    const bool has_value = (equal_pos != std::string::npos);
    std::string keyword;
    if (has_value) {
      value = absl::StripAsciiWhitespace(option_str.substr(equal_pos + 1));
      keyword = absl::StripAsciiWhitespace(option_str.substr(0, equal_pos));
    } else {
      keyword = absl::StripAsciiWhitespace(option_str);
    }
    absl::AsciiStrToLower(&keyword);

    bool is_negated = false;
    auto it = options_by_lower_keyword_.find(keyword);
    if (it == options_by_lower_keyword_.end() &&
        absl::StartsWith(keyword, "no_")) {
      keyword.erase(0, 3);
      is_negated = true;
      it = options_by_lower_keyword_.find(keyword);
    }
    if (it == options_by_lower_keyword_.end()) {
      return ::file_based_test_driver_base::UnknownErrorBuilder().LogError()
             << "Keyword [" << keyword << "] does not exist.";
    }
    TestCaseOption& option = it->second;

    TestCaseOptionValue* option_value;
    if (set_default) {
      option_value = &option.default_value;
      option.default_was_parsed = true;
    } else {
      option_value = &option.current_value;
    }
    switch (option.type) {
      case TestCaseOption::kBool:
        if (has_value) {
          return ::file_based_test_driver_base::UnknownErrorBuilder().LogError()
                 << "Bool keyword [" << keyword
                 << "] cannot take a value; use keyword "
                 << "and no_keyword instead";
        }
        option_value->bool_value = !is_negated;
        break;
      case TestCaseOption::kString:
        if (is_negated) {
          return ::file_based_test_driver_base::UnknownErrorBuilder().LogError()
                 << "String keyword [" << keyword
                 << "] cannot be negated with 'no_'";
        }
        if (!has_value) {
          return ::file_based_test_driver_base::UnknownErrorBuilder().LogError()
                 << "String keyword [" << keyword << "] requires a value";
        }
        option_value->string_value = value;
        break;
      case TestCaseOption::kInt64:
        if (is_negated) {
          return ::file_based_test_driver_base::UnknownErrorBuilder().LogError()
                 << "Int64 keyword [" << keyword
                 << "] cannot be negated with 'no_'";
        }
        if (!has_value) {
          return ::file_based_test_driver_base::UnknownErrorBuilder().LogError()
                 << "Int64 keyword [" << keyword << "] requires a value";
        }
        if (!absl::SimpleAtoi(value, &option_value->int64_value)) {
          return ::file_based_test_driver_base::UnknownErrorBuilder().LogError()
                 << "Invalid value for int64_t keyword [" << keyword << "] ";
        }
        break;
      case TestCaseOption::kDuration:
        if (is_negated) {
          return ::file_based_test_driver_base::UnknownErrorBuilder().LogError()
                 << "Duration keyword [" << keyword
                 << "] cannot be negated with 'no_'";
        }
        if (!has_value) {
          return ::file_based_test_driver_base::UnknownErrorBuilder().LogError()
                 << "Duration keyword [" << keyword << "] requires a value";
        }
        if (!absl::ParseDuration(value, &option_value->duration_value)) {
          return ::file_based_test_driver_base::UnknownErrorBuilder().LogError()
                 << "Invalid value for duration keyword [" << keyword << "] ";
        }
        break;
    }
    if (!set_default) {
      option.current_value.is_set_explicitly = true;
    }
  }

  return absl::OkStatus();
}

}  // namespace file_based_test_driver

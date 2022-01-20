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
// TestCaseOptions is a utility for text-based test cases (such as the ones
// processed by RunTestCasesFromFiles in file_based_test_driver.h). It supports
// parsing text options from the beginning of a test case. These options can be
// used to specify things like the context (e.g. flags), test type, input
// format, requested output type, and any other parameters that are separate
// from the test case body.
//
// Options can be of type bool, string, or int64_t. Options are parsed from the
// start of the test case, and they must be enclosed in square brackets.
// Boolean option values are specified as [option] or [no_option]; string and
// integer options as [option=value]. It is allowed to have spaces around option
// name and value: [ option = value ] and to have a multiline value:
// [option_list =
//    long_value_1,
//    long_value_2]
// In that case all extra spaces around the value are trimmed.
//
// Each option has a default value. The default can be modified by an option of
// the form [default option=value]. An option of this form sets the value of
// "option" *and* stores it as the new default. This new default is set for the
// lifetime of the TestCaseOptions object. If you are using text-based test case
// files that can contain multiple test cases, then you probably want the
// [default X] construct to modify the option's default value for the remainder
// of the input file. To achieve that, you should normally create exactly one
// TestCaseOptions object for each input file. If you share a TestCaseOptions
// instance between input files, then modified defaults may carry over. It is
// good style to only override defaults in the first test case of a test file.
// (Note that this advice doesn't play well with RunTestCasesFromFiles() when
// used with a file glob instead of an individual file, because it doesn't tell
// you about the file transitions. If this is an issue, you must do the globbing
// yourself.)
//
// The available set of options is predetermined by the object's owner, which
// must register all options beforehand. Options are identified by an enum value
// in code, and by a string in the test case. The enum type is specified as a
// template parameter of TestCaseOptions.
//
// Test case example:
//
// [some_bool_option][no_some_other_bool_option]
// [some_string_option=my_string]
// ...actual test case...
//
// Usage:
//   TestCaseOptions options;
//   options.RegisterBool("some_bool_option", false /* default_value */);
//   ...etc.
//   RunTestCasesFromFiles( ...);
//
//   // This strips the options off the test case.
//   absl::Status status = options.ParseTestCaseOptions(&test_case);
//   if (!status.ok() {
//     // handle error
//   }
//   /// ... and use options:
//   const bool apply_some_bool_option = options.GetBool("some_bool_option");
//
#ifndef THIRD_PARTY_FILE_BASED_TEST_DRIVER_TEST_CASE_OPTIONS_H_
#define THIRD_PARTY_FILE_BASED_TEST_DRIVER_TEST_CASE_OPTIONS_H_

#include <string>
#include <utility>
#include <vector>

#include "file_based_test_driver/base/logging.h"
#include <cstdint>
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/strings/ascii.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"

namespace file_based_test_driver {

// Implementation detail for the templated TestCaseOptions class. Do not use
// directly.
namespace internal {

// Contains a single option value.
struct TestCaseOptionValue {
  explicit TestCaseOptionValue(bool bool_value_in)
      : bool_value(bool_value_in) {}

  explicit TestCaseOptionValue(std::string string_value_in)
      : string_value(std::move(string_value_in)) {}

  explicit TestCaseOptionValue(int64_t int64_value_in)
      : int64_value(int64_value_in) {}

  explicit TestCaseOptionValue(absl::Duration duration_value_in)
      : duration_value(duration_value_in) {}

  // Value semantics.
  TestCaseOptionValue(const TestCaseOptionValue&) = default;
  TestCaseOptionValue(TestCaseOptionValue&&) = default;

  TestCaseOptionValue& operator=(const TestCaseOptionValue&) = default;
  TestCaseOptionValue& operator=(TestCaseOptionValue&&) = default;

  bool bool_value = false;
  std::string string_value;
  int64_t int64_value = 0;
  absl::Duration duration_value = absl::ZeroDuration();

  // If this is true, then the value was set explicitly in the current test
  // case.
  bool is_set_explicitly = false;
};

// Contains the current and default values for an option, as well as its
// keyword.
struct TestCaseOption {
  enum Type {
    kString = 1,
    kBool,
    kInt64,
    kDuration,
  };

  TestCaseOption(absl::string_view keyword_, bool bool_value)
      : keyword(keyword_),
        type(kBool),
        default_value(bool_value),
        current_value(bool_value) {}

  TestCaseOption(absl::string_view keyword_, const std::string& str)
      : keyword(keyword_),
        type(kString),
        default_value(str),
        current_value(str) {}

  TestCaseOption(absl::string_view keyword_, int64_t int_value)
      : keyword(keyword_),
        type(kInt64),
        default_value(int_value),
        current_value(int_value) {}

  TestCaseOption(absl::string_view keyword_, absl::Duration duration_value)
      : keyword(keyword_),
        type(kDuration),
        default_value(duration_value),
        current_value(duration_value) {}

  // Value semantics.
  TestCaseOption(const TestCaseOption&) = default;
  TestCaseOption(TestCaseOption&&) = default;

  TestCaseOption& operator=(const TestCaseOption&) = default;
  TestCaseOption& operator=(TestCaseOption&&) = default;

  std::string keyword;
  Type type;
  TestCaseOptionValue default_value;
  TestCaseOptionValue current_value;

  // If true, 'default_value' was parsed from a string passed to
  // TestCaseOptions::ParseTestCaseOptions(), and is not necessarily the same
  // default value that was initially supplied to
  // RegisterBool()/RegisterString()/RegisterInt64().
  bool default_was_parsed = false;
};

}  // namespace internal


// See file comment.
class TestCaseOptions {
 public:
  using TestCaseOption = internal::TestCaseOption;
  using TestCaseOptionValue = internal::TestCaseOptionValue;

  TestCaseOptions() = default;

  // Disallow copy and assign.
  TestCaseOptions(const TestCaseOptions&) = delete;
  TestCaseOptions& operator=(const TestCaseOptions&) = delete;

  // Registers a boolean option. In a parsed test case, the value may be set
  // with [keyword] and [no_keyword], for true and false respectively.
  void RegisterBool(absl::string_view keyword, bool default_value) {
    const std::string keyword_lower = absl::AsciiStrToLower(keyword);
    FILE_BASED_TEST_DRIVER_CHECK(options_by_lower_keyword_.try_emplace(
        keyword_lower, keyword_lower, default_value).second);
  }

  // Registers a string option. In a parsed test case, the
  // value may be set with [keyword=some string].
  void RegisterString(absl::string_view keyword, std::string default_value) {
    const std::string keyword_lower = absl::AsciiStrToLower(keyword);
    FILE_BASED_TEST_DRIVER_CHECK(options_by_lower_keyword_.try_emplace(
        keyword_lower, keyword_lower, std::move(default_value)).second);
  }

  // Registers an int64_t option. In a parsed test case, the
  // value may be set with [keyword=123456].
  void RegisterInt64(absl::string_view keyword, int64_t default_value) {
    const std::string keyword_lower = absl::AsciiStrToLower(keyword);
    FILE_BASED_TEST_DRIVER_CHECK(options_by_lower_keyword_.try_emplace(
        keyword_lower, keyword_lower, default_value).second);
  }

  // Registers a duration option. In a parsed test case, the
  // value may be set with [keyword=22m].
  void RegisterDuration(absl::string_view keyword,
                        absl::Duration default_value) {
    const std::string keyword_lower = absl::AsciiStrToLower(keyword);
    FILE_BASED_TEST_DRIVER_CHECK(options_by_lower_keyword_
              .try_emplace(keyword_lower, keyword_lower, default_value)
              .second);
  }

  bool GetBool(absl::string_view option_keyword) const {
    return GetCurrentValueOrDie(TestCaseOption::kBool, option_keyword)
        ->bool_value;
  }

  const std::string& GetString(absl::string_view option_keyword) const {
    return GetCurrentValueOrDie(TestCaseOption::kString, option_keyword)
        ->string_value;
  }

  int64_t GetInt64(absl::string_view option_keyword) const {
    return GetCurrentValueOrDie(TestCaseOption::kInt64, option_keyword)
        ->int64_value;
  }

  absl::Duration GetDuration(absl::string_view option_keyword) const {
    return GetCurrentValueOrDie(TestCaseOption::kDuration, option_keyword)
        ->duration_value;
  }

  // Overrides the current value for an option with keyword <option_keyword>
  // (case insensitive). The option will remain set until the next call to
  // ParseTestCaseOptions. There is a separate function for each option type.
  void SetBool(absl::string_view option_keyword, bool value) {
    TestCaseOptionValue* option_value =
        GetMutableCurrentValueOrDie(TestCaseOption::kBool, option_keyword);
    option_value->bool_value = value;
    option_value->is_set_explicitly = true;
  }
  void SetString(absl::string_view option_keyword, const std::string& value) {
    TestCaseOptionValue* option_value =
        GetMutableCurrentValueOrDie(TestCaseOption::kString, option_keyword);
    option_value->string_value = value;
    option_value->is_set_explicitly = true;
  }
  void SetInt64(absl::string_view option_keyword, int64_t value) {
    TestCaseOptionValue* option_value =
        GetMutableCurrentValueOrDie(TestCaseOption::kInt64, option_keyword);
    option_value->int64_value = value;
    option_value->is_set_explicitly = true;
  }

  void SetDuration(absl::string_view option_keyword, absl::Duration value) {
    TestCaseOptionValue* option_value =
        GetMutableCurrentValueOrDie(TestCaseOption::kDuration, option_keyword);
    option_value->duration_value = value;
    option_value->is_set_explicitly = true;
  }

  // Returns true if the value for <option_keyword> has been set explicitly.
  bool IsExplicitlySet(absl::string_view option_keyword) const {
    auto it = options_by_lower_keyword_.find(
        absl::AsciiStrToLower(option_keyword));
    FILE_BASED_TEST_DRIVER_CHECK(it != options_by_lower_keyword_.end())
        << "Unknown option: " << option_keyword;
    return it->second.current_value.is_set_explicitly;
  }

  // Returns true if the default value for <option_keyword> was parsed from a
  // string passed to ParseTestCaseOptions() (meaning that it the default is
  // no longer necessarily the same default that was originally passed to
  // RegisterBool(), RegisterInt64(), or RegisterString()).
  bool DefaultWasParsed(absl::string_view option_keyword) const {
    auto it = options_by_lower_keyword_.find(
        absl::AsciiStrToLower(option_keyword));
    FILE_BASED_TEST_DRIVER_CHECK(it != options_by_lower_keyword_.end())
        << "Unknown option: " << option_keyword;
    return it->second.default_was_parsed;
  }

  // Resets all options to their defaults, strips leading whitespace off of
  // <*str> and parses any [option] lines out. The remaining string is left in
  // <*str>.
  // If 'allow_defaults' is false, attempts to set default values with
  // [default option] will result in an error.
  // If 'defaults_found' is not nullptr, it will be used as a return value to
  // indicate whether or not default values where set in the test case.
  absl::Status ParseTestCaseOptions(std::string* str, bool allow_defaults,
                                    bool* defaults_found);

  // Resets all options to their defaults, strips leading whitespace off of
  // <*str> and parses any [option] lines out. The remaining string is left in
  // <*str>.
  absl::Status ParseTestCaseOptions(std::string* str) {
    return ParseTestCaseOptions(str, /*allow_defaults=*/true,
                                /*defaults_found=*/nullptr);
  }

 private:
  // Returns the current value of the option with keyword <option_keyword>
  // (case insensitive). Crashes if the option does not exist or is not of type
  // <type>.
  const TestCaseOptionValue* GetCurrentValueOrDie(
      TestCaseOption::Type type, absl::string_view option_keyword) const {
    const std::string keyword_lower = absl::AsciiStrToLower(option_keyword);
    auto it = options_by_lower_keyword_.find(keyword_lower);
    FILE_BASED_TEST_DRIVER_CHECK(it != options_by_lower_keyword_.end())
        << "Unknown option: " << keyword_lower;
    FILE_BASED_TEST_DRIVER_CHECK_EQ(it->second.type, type) << "Invalid keyword type requested";
    return &it->second.current_value;
  }

  TestCaseOptionValue* GetMutableCurrentValueOrDie(
      TestCaseOption::Type type, absl::string_view option_keyword) {
    return const_cast<TestCaseOptionValue*>(
        GetCurrentValueOrDie(type, option_keyword));
  }

  // Sets option values using option strings in <option_strings>. Option strings
  // must be of the form "option_keyword=value", with no "default" prefix --
  // that should have been stripped off. If <set_default> is true, stores the
  // values as the default values for the options. If <set_default> is false,
  // stores the values as the current values for the options, and marks the
  // options as being explicitly set.
  absl::Status SetOptionValuesFromStrings(
      const std::vector<std::string>& option_strings, bool set_default);

  // Extracts test case option strings of the form "[...]" from the start of
  // <*str> and adds them to <*option_strings>. Removes extracted option strings
  // from <*str>. Whitespace before, between and after the option strings is
  // ignored and stripped.
  absl::Status ExtractAndRemoveOptionStrings(
      std::string* str, std::vector<std::string>* option_strings);

  absl::flat_hash_map<std::string, TestCaseOption> options_by_lower_keyword_;
};

}  // namespace file_based_test_driver

#endif  // THIRD_PARTY_FILE_BASED_TEST_DRIVER_TEST_CASE_OPTIONS_H_

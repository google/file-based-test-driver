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
#include "file_based_test_driver/test_case_options.h"

#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/time/time.h"
#include "file_based_test_driver/base/status_matchers.h"

namespace file_based_test_driver {

using ::file_based_test_driver::testing::StatusIs;
using ::testing::_;
using ::testing::HasSubstr;

namespace {

const char* kSomeStringOption = "some_string";
const char* kSomeBoolOption = "some_bool";
const char* kSomeIntOption = "some_int";
const char* kSomeDurationOption = "some_duration";
const char* kBoolDefaultTrueOption = "bool_def_true";

}  // anonymous namespace

// Verifies that every option registered by the SuccessfulParsing test has its
// default value and is not explicitly set.
static void CheckSuccessfulParsingHasDefaultValues(
    const TestCaseOptions& options) {
  EXPECT_EQ("cow", options.GetString(kSomeStringOption));
  EXPECT_FALSE(options.GetBool(kSomeBoolOption));
  EXPECT_TRUE(options.GetBool(kBoolDefaultTrueOption));
  EXPECT_EQ(55, options.GetInt64(kSomeIntOption));
  EXPECT_EQ(absl::Minutes(22), options.GetDuration(kSomeDurationOption));

  EXPECT_FALSE(options.IsExplicitlySet(kSomeStringOption));
  EXPECT_FALSE(options.IsExplicitlySet(kSomeBoolOption));
  EXPECT_FALSE(options.IsExplicitlySet(kBoolDefaultTrueOption));
  EXPECT_FALSE(options.IsExplicitlySet(kSomeIntOption));
  EXPECT_FALSE(options.IsExplicitlySet(kSomeDurationOption));
}

TEST(TestOptionsTest, SuccessfulParsing) {
  TestCaseOptions options;
  options.RegisterString(kSomeStringOption, "cow");
  options.RegisterBool(kSomeBoolOption, false);
  options.RegisterBool(kBoolDefaultTrueOption, true);
  options.RegisterInt64(kSomeIntOption, 55);
  options.RegisterDuration(kSomeDurationOption, absl::Minutes(22));
  std::string test_case_str;

  // Parse with no values set.
  test_case_str = "This is my test";
  FILE_BASED_TEST_DRIVER_EXPECT_OK(options.ParseTestCaseOptions(&test_case_str));
  CheckSuccessfulParsingHasDefaultValues(options);

  // Parse some options.
  test_case_str =
      "[some_string=foo][some_bool][some_int=66][some_duration=1s]"
      "\ntest2";
  FILE_BASED_TEST_DRIVER_EXPECT_OK(options.ParseTestCaseOptions(&test_case_str));
  EXPECT_EQ("test2", test_case_str);
  EXPECT_EQ("foo", options.GetString(kSomeStringOption));
  EXPECT_TRUE(options.GetBool(kSomeBoolOption));
  EXPECT_TRUE(options.GetBool(kBoolDefaultTrueOption));
  EXPECT_EQ(66, options.GetInt64(kSomeIntOption));
  EXPECT_EQ(absl::Seconds(1), options.GetDuration(kSomeDurationOption));

  EXPECT_TRUE(options.IsExplicitlySet(kSomeBoolOption));
  EXPECT_TRUE(options.IsExplicitlySet(kSomeStringOption));
  EXPECT_TRUE(options.IsExplicitlySet(kSomeIntOption));
  EXPECT_TRUE(options.IsExplicitlySet(kSomeDurationOption));

  // Parse nested [ and ].
  test_case_str = "[some_int=66][some_string=[foo][bar]][some_bool]\ntest2.1";
  FILE_BASED_TEST_DRIVER_EXPECT_OK(options.ParseTestCaseOptions(&test_case_str));
  EXPECT_EQ("test2.1", test_case_str);
  EXPECT_EQ("[foo][bar]", options.GetString(kSomeStringOption));
  EXPECT_TRUE(options.GetBool(kSomeBoolOption));
  EXPECT_TRUE(options.GetBool(kBoolDefaultTrueOption));
  EXPECT_EQ(66, options.GetInt64(kSomeIntOption));

  // Parse multi-nested [ and ].
  test_case_str = "[some_string=foo[bar[foo]][bar]]\ntest2.2";
  FILE_BASED_TEST_DRIVER_EXPECT_OK(options.ParseTestCaseOptions(&test_case_str));
  EXPECT_EQ("test2.2", test_case_str);
  EXPECT_EQ("foo[bar[foo]][bar]", options.GetString(kSomeStringOption));

  // Unclosed [.
  test_case_str = "[some_string=\ntest2.3";
  EXPECT_THAT(
      options.ParseTestCaseOptions(&test_case_str),
      StatusIs(_, HasSubstr("Unclosed [ while processing TestCaseOptions")));

  // Unmatching [ and ].
  test_case_str = "[some_string=[]\ntest2.4";
  EXPECT_THAT(
      options.ParseTestCaseOptions(&test_case_str),
      StatusIs(_, HasSubstr("Unclosed [ while processing TestCaseOptions")));

  // Extra ].
  test_case_str = "[some_string=[]]]\ntest2.5";
  FILE_BASED_TEST_DRIVER_EXPECT_OK(options.ParseTestCaseOptions(&test_case_str));
  EXPECT_EQ("]\ntest2.5", test_case_str);

  // Parse again with no options. Should get all defaults.
  test_case_str = "  test3";
  FILE_BASED_TEST_DRIVER_EXPECT_OK(options.ParseTestCaseOptions(&test_case_str));
  EXPECT_EQ("test3", test_case_str);
  CheckSuccessfulParsingHasDefaultValues(options);

  // Again, test negating booleans and parsing an empty string,
  // as well as case-insensitive keyword match.
  test_case_str = "[some_String=][NO_BOOL_def_true]\ntest4";
  FILE_BASED_TEST_DRIVER_EXPECT_OK(options.ParseTestCaseOptions(&test_case_str));
  EXPECT_EQ("test4", test_case_str);
  EXPECT_EQ("", options.GetString(kSomeStringOption));
  EXPECT_FALSE(options.GetBool(kSomeBoolOption));
  EXPECT_FALSE(options.GetBool(kBoolDefaultTrueOption));
  EXPECT_EQ(55, options.GetInt64(kSomeIntOption));
  EXPECT_EQ(absl::Minutes(22), options.GetDuration(kSomeDurationOption));

  EXPECT_FALSE(options.IsExplicitlySet(kSomeBoolOption));
  EXPECT_TRUE(options.IsExplicitlySet(kSomeStringOption));
  EXPECT_FALSE(options.IsExplicitlySet(kSomeIntOption));
  EXPECT_FALSE(options.IsExplicitlySet(kSomeDurationOption));
  EXPECT_TRUE(options.IsExplicitlySet(kBoolDefaultTrueOption));

  // Explicitly set some values.
  options.SetBool(kSomeBoolOption, true);
  EXPECT_TRUE(options.GetBool(kSomeBoolOption));
  EXPECT_TRUE(options.IsExplicitlySet(kSomeBoolOption));

  options.SetInt64(kSomeIntOption, 10000);
  EXPECT_EQ(10000, options.GetInt64(kSomeIntOption));
  EXPECT_TRUE(options.IsExplicitlySet(kSomeIntOption));

  options.SetDuration(kSomeDurationOption, absl::Hours(1));
  EXPECT_EQ(absl::Hours(1), options.GetDuration(kSomeDurationOption));
  EXPECT_TRUE(options.IsExplicitlySet(kSomeDurationOption));

  options.SetString(kSomeStringOption, "set it");
  EXPECT_EQ("set it", options.GetString(kSomeStringOption));
  EXPECT_TRUE(options.IsExplicitlySet(kSomeStringOption));

  // Parse again; this should reset even explicitly set options.
  test_case_str = "test5";
  FILE_BASED_TEST_DRIVER_EXPECT_OK(options.ParseTestCaseOptions(&test_case_str));
  CheckSuccessfulParsingHasDefaultValues(options);
}

TEST(TestOptionsTest, FailedParsing) {
  TestCaseOptions options;
  options.RegisterString(kSomeStringOption, "cow");
  options.RegisterBool(kSomeBoolOption, false);
  options.RegisterInt64(kSomeIntOption, 55);
  options.RegisterDuration(kSomeDurationOption, absl::Minutes(22));
  std::string test_case_str;

  test_case_str = "[no_some_string]";
  EXPECT_THAT(
      options.ParseTestCaseOptions(&test_case_str),
      StatusIs(
          _, HasSubstr(
                 "String keyword [some_string] cannot be negated with 'no_'")));

  test_case_str = "[some_string]";
  EXPECT_THAT(
      options.ParseTestCaseOptions(&test_case_str),
      StatusIs(_, HasSubstr("String keyword [some_string] requires a value")));

  test_case_str = "[some_bool=true]";
  EXPECT_THAT(
      options.ParseTestCaseOptions(&test_case_str),
      StatusIs(_, HasSubstr("Bool keyword [some_bool] cannot take a value; "
                            "use keyword and no_keyword instead")));

  test_case_str = "[some_int]";
  EXPECT_THAT(
      options.ParseTestCaseOptions(&test_case_str),
      StatusIs(_, HasSubstr("Int64 keyword [some_int] requires a value")));

  test_case_str = "[no_some_int]";
  EXPECT_THAT(
      options.ParseTestCaseOptions(&test_case_str),
      StatusIs(
          _,
          HasSubstr("Int64 keyword [some_int] cannot be negated with 'no_'")));

  test_case_str = "[some_int=ab]";
  EXPECT_THAT(
      options.ParseTestCaseOptions(&test_case_str),
      StatusIs(_, HasSubstr("Invalid value for int64_t keyword [some_int]")));

  test_case_str = "[some_duration]";
  EXPECT_THAT(
      options.ParseTestCaseOptions(&test_case_str),
      StatusIs(_,
               HasSubstr("Duration keyword [some_duration] requires a value")));

  test_case_str = "[no_some_duration]";
  EXPECT_THAT(
      options.ParseTestCaseOptions(&test_case_str),
      StatusIs(_, HasSubstr("Duration keyword [some_duration] cannot be "
                            "negated with 'no_'")));
  test_case_str = "[some_duration=ab]";
  EXPECT_THAT(
      options.ParseTestCaseOptions(&test_case_str),
      StatusIs(
          _, HasSubstr("Invalid value for duration keyword [some_duration]")));
}

TEST(TestOptionsTest, SetDefaultOptions) {
  TestCaseOptions options;
  options.RegisterString(kSomeStringOption, "cow");
  options.RegisterBool(kSomeBoolOption, false);
  std::string test_case_str;

  test_case_str = "[some_string=horse][default some_bool]";
  FILE_BASED_TEST_DRIVER_EXPECT_OK(options.ParseTestCaseOptions(&test_case_str));
  EXPECT_EQ("horse", options.GetString(kSomeStringOption));
  EXPECT_EQ(true, options.GetBool(kSomeBoolOption));

  test_case_str = "[some_string=octobruary]";
  FILE_BASED_TEST_DRIVER_EXPECT_OK(options.ParseTestCaseOptions(&test_case_str));
  EXPECT_EQ("octobruary", options.GetString(kSomeStringOption));
  EXPECT_EQ(true, options.GetBool(kSomeBoolOption));

  test_case_str = "[some_string=horse][default no_some_bool]";
  FILE_BASED_TEST_DRIVER_EXPECT_OK(options.ParseTestCaseOptions(&test_case_str));
  EXPECT_EQ("horse", options.GetString(kSomeStringOption));
  EXPECT_EQ(false, options.GetBool(kSomeBoolOption));
  EXPECT_EQ(false, options.IsExplicitlySet(kSomeBoolOption));

  test_case_str = "[some_string=octobruary]";
  FILE_BASED_TEST_DRIVER_EXPECT_OK(options.ParseTestCaseOptions(&test_case_str));
  EXPECT_EQ("octobruary", options.GetString(kSomeStringOption));
  EXPECT_EQ(false, options.GetBool(kSomeBoolOption));
  EXPECT_EQ(false, options.IsExplicitlySet(kSomeBoolOption));

  // The [default ...] option only sets the default, so it's still possible to
  // override it in the same set of options.
  test_case_str = "[some_string=notthedefault][default some_string=thedefault]";
  FILE_BASED_TEST_DRIVER_EXPECT_OK(options.ParseTestCaseOptions(&test_case_str));
  EXPECT_EQ("notthedefault", options.GetString(kSomeStringOption));

  test_case_str = "[some_bool]";
  FILE_BASED_TEST_DRIVER_EXPECT_OK(options.ParseTestCaseOptions(&test_case_str));
  EXPECT_EQ("thedefault", options.GetString(kSomeStringOption));
  EXPECT_EQ(true, options.GetBool(kSomeBoolOption));
  EXPECT_EQ(true, options.IsExplicitlySet(kSomeBoolOption));
}

TEST(TestOptionsTest, DisallowedDefaultOptions) {
  TestCaseOptions options;
  options.RegisterString(kSomeStringOption, "cow");
  options.RegisterBool(kSomeBoolOption, false);
  std::string test_case_str;

  bool defaults_found;

  test_case_str = "[some_string=horse][default some_bool]";
  FILE_BASED_TEST_DRIVER_EXPECT_OK(options.ParseTestCaseOptions(
      &test_case_str, true /* allow_defaults */, &defaults_found));
  EXPECT_EQ(defaults_found, true);

  test_case_str = "[some_string=horse][default some_bool]";
  // Ensure test covers overwriting false with true.
  defaults_found = false;
  EXPECT_THAT(options.ParseTestCaseOptions(
                  &test_case_str, false /* allow_defaults */, &defaults_found),
              StatusIs(_, HasSubstr("defaults are not allowed")));
  EXPECT_EQ(defaults_found, false);

  test_case_str = "[some_string=horse][some_bool]";
  // Ensure test covers overwriting true with false.
  defaults_found = true;
  FILE_BASED_TEST_DRIVER_EXPECT_OK(
      options.ParseTestCaseOptions(&test_case_str, true /* allow_defaults */,
                                   &defaults_found));
  EXPECT_EQ(defaults_found, false);
}

TEST(TestOptionsTest, TrimsWhitespaces) {
  TestCaseOptions options;
  options.RegisterString(kSomeStringOption, "cow");
  options.RegisterBool(kSomeBoolOption, false);
  options.RegisterInt64(kSomeIntOption, 55);
  options.RegisterDuration(kSomeDurationOption, absl::Minutes(22));
  std::string test_case_str;

  test_case_str =
      "[some_string =\n"
      "  foo\n"
      "]\n"
      "[ some_bool ]\n"
      "[some_int = 66]\n"
      "[ some_duration = 1s ]";
  FILE_BASED_TEST_DRIVER_EXPECT_OK(options.ParseTestCaseOptions(&test_case_str));
  EXPECT_EQ("foo", options.GetString(kSomeStringOption));
  EXPECT_TRUE(options.GetBool(kSomeBoolOption));
  EXPECT_EQ(66, options.GetInt64(kSomeIntOption));
  EXPECT_EQ(absl::Seconds(1), options.GetDuration(kSomeDurationOption));

  EXPECT_TRUE(options.IsExplicitlySet(kSomeBoolOption));
  EXPECT_TRUE(options.IsExplicitlySet(kSomeStringOption));
  EXPECT_TRUE(options.IsExplicitlySet(kSomeIntOption));
  EXPECT_TRUE(options.IsExplicitlySet(kSomeDurationOption));
}

}  // namespace file_based_test_driver

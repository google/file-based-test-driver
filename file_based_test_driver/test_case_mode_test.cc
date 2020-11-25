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
#include "file_based_test_driver/test_case_mode.h"

#include <string>
#include <tuple>
#include <type_traits>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "file_based_test_driver/base/status_matchers.h"
#include "file_based_test_driver/base/status_macros.h"

namespace file_based_test_driver {

namespace {

using ::file_based_test_driver::testing::IsOk;
using ::file_based_test_driver::testing::IsOkAndHolds;
using ::file_based_test_driver::testing::StatusIs;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::IsEmpty;
using ::testing::Not;

class TestCaseModeTest : public ::testing::Test {};

TEST_F(TestCaseModeTest, Create) {
  EXPECT_THAT(TestCaseMode::Create(""),
              StatusIs(absl::StatusCode::kFailedPrecondition));
  EXPECT_THAT(TestCaseMode::Create(std::vector<std::string>({})), IsOk());
  EXPECT_THAT(TestCaseMode::Create("Foo Bar"), IsOk());
  EXPECT_THAT(TestCaseMode::Create(std::vector<std::string>({"Foo Bar"})),
              StatusIs(absl::StatusCode::kFailedPrecondition));
  EXPECT_THAT(TestCaseMode::Create(std::vector<std::string>({"Foo", "Bar"})),
              IsOk());
  EXPECT_THAT(
      TestCaseMode::Create(std::vector<std::string>({"Foo", "Bar Baz"})),
      StatusIs(absl::StatusCode::kFailedPrecondition));
  EXPECT_THAT(TestCaseMode::Create("Foo*Bar"), Not(IsOk()));
  EXPECT_THAT(TestCaseMode::Create(std::vector<std::string>({"Foo*Bar"})),
              StatusIs(absl::StatusCode::kFailedPrecondition));
}

TEST_F(TestCaseModeTest, Parse) {
  FILE_BASED_TEST_DRIVER_ASSERT_OK_AND_ASSIGN(TestCaseMode test_case_foo, TestCaseMode::Create("FOO"));
  FILE_BASED_TEST_DRIVER_ASSERT_OK_AND_ASSIGN(TestCaseMode test_case_bar, TestCaseMode::Create("BAR"));
  FILE_BASED_TEST_DRIVER_ASSERT_OK_AND_ASSIGN(
      TestCaseMode test_case_foo_bar,
      TestCaseMode::Create(std::vector<std::string>({"FOO", "BAR"})));

  EXPECT_THAT(TestCaseMode::ParseModes(""),
              IsOkAndHolds(IsEmpty()));
  EXPECT_THAT(TestCaseMode::ParseModes("[FOO]"),
              IsOkAndHolds(ElementsAre(test_case_foo)));
  EXPECT_THAT(
      TestCaseMode::ParseModes("[FOO][BAR]"),
      IsOkAndHolds(ElementsAre(test_case_foo, test_case_bar)));

  // Whitespace is ignored.
  EXPECT_THAT(TestCaseMode::ParseModes(" [FOO]"),
              IsOkAndHolds(ElementsAre(test_case_foo)));
  EXPECT_THAT(TestCaseMode::ParseModes("[FOO] "),
              IsOkAndHolds(ElementsAre(test_case_foo)));
  EXPECT_THAT(
      TestCaseMode::ParseModes("[FOO] [BAR]"),
      IsOkAndHolds(ElementsAre(test_case_foo, test_case_bar)));

  // Spaces breaks up a mode into pieces.
  EXPECT_THAT(TestCaseMode::ParseModes("[FOO BAR]"),
              IsOkAndHolds(ElementsAre(test_case_foo_bar)));

  EXPECT_THAT(TestCaseMode::ParseModes("FOO"),
              StatusIs(absl::StatusCode::kUnknown));
  EXPECT_THAT(TestCaseMode::ParseModes("{FOO}"),
              StatusIs(absl::StatusCode::kUnknown));
  EXPECT_THAT(TestCaseMode::ParseModes("+[FOO]"),
              StatusIs(absl::StatusCode::kUnknown));
  EXPECT_THAT(TestCaseMode::ParseModes("[FOO]/[BAR]"),
              StatusIs(absl::StatusCode::kUnknown));
  EXPECT_THAT(TestCaseMode::ParseModes("[FOO"),
              StatusIs(absl::StatusCode::kUnknown));
  EXPECT_THAT(TestCaseMode::ParseModes("[FOO\\]BAR]"),
              StatusIs(absl::StatusCode::kUnknown));
  EXPECT_THAT(TestCaseMode::ParseModes("[FOO *]"),
              StatusIs(absl::StatusCode::kFailedPrecondition));
}

TEST_F(TestCaseModeTest, Collapse) {
  // "All Modes" collpased to empty string.
  TestCaseMode all_modes;
  EXPECT_THAT(
      TestCaseMode::CollapseModes(TestCaseMode::Set({all_modes})), Eq(""));

  FILE_BASED_TEST_DRIVER_ASSERT_OK_AND_ASSIGN(TestCaseMode test_case_foo, TestCaseMode::Create("FOO"));
  FILE_BASED_TEST_DRIVER_ASSERT_OK_AND_ASSIGN(TestCaseMode test_case_bar, TestCaseMode::Create("BAR"));
  FILE_BASED_TEST_DRIVER_ASSERT_OK_AND_ASSIGN(
      TestCaseMode test_case_foo_bar,
      TestCaseMode::Create(std::vector<std::string>({"FOO", "BAR"})));

  // Simple test.
  EXPECT_THAT(TestCaseMode::CollapseModes(TestCaseMode::Set({test_case_foo})),
              Eq("[FOO]"));

  // Vector collapses.
  EXPECT_THAT(
      TestCaseMode::CollapseModes(TestCaseMode::Set({test_case_foo_bar})),
      Eq("[FOO BAR]"));

  // Order is stable.
  EXPECT_THAT(TestCaseMode::CollapseModes(
                  TestCaseMode::Set({test_case_foo, test_case_bar})),
              Eq("[BAR][FOO]"));
  EXPECT_THAT(TestCaseMode::CollapseModes(
                  TestCaseMode::Set({test_case_bar, test_case_foo})),
              Eq("[BAR][FOO]"));
}

}  // namespace

}  // namespace file_based_test_driver

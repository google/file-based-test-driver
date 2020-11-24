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
#include "test_case_outputs.h"

#include <string>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "base/status_matchers.h"
#include "test_case_mode.h"
#include "base/status_macros.h"

using ::file_based_test_driver::testing::StatusIs;
using ::testing::_;
using ::testing::ContainerEq;
using ::testing::HasSubstr;

namespace file_based_test_driver {

class TestCaseOutputsTest : public ::testing::Test {};

TEST_F(TestCaseOutputsTest, AllModesMainOutput) {
  TestCaseOutputs outputs;
  std::vector<std::string> parts = {
      "main test output",
  };

  FILE_BASED_TEST_DRIVER_ASSERT_OK(outputs.ParseFrom(parts));

  std::vector<std::string> actual_outputs;
  FILE_BASED_TEST_DRIVER_ASSERT_OK(outputs.GetCombinedOutputs(false /* include_possible_modes*/,
                                       &actual_outputs));
  EXPECT_THAT(actual_outputs, ContainerEq(parts));
}

TEST_F(TestCaseOutputsTest, WithResultTypeAndTestModes) {
  TestCaseOutputs outputs;
  std::vector<std::string> parts = {
      "<>[MODE 1][MODE_2]\ntest output line 1\ntest output line2",
      "<TYPE_A>[MODE1]\ntest output 2\n",
      "<TYPE B>\ntest output 3",
  };

  std::vector<std::string> expected = {
      "<>[MODE 1][MODE_2]\ntest output line 1\ntest output line2",
      "<TYPE B>\ntest output 3",
      "<TYPE_A>[MODE1]\ntest output 2\n",
  };

  FILE_BASED_TEST_DRIVER_ASSERT_OK(outputs.ParseFrom(parts));

  std::vector<std::string> actual_outputs;
  FILE_BASED_TEST_DRIVER_ASSERT_OK(outputs.GetCombinedOutputs(false /* include_possible_modes*/,
                                       &actual_outputs));
  EXPECT_THAT(actual_outputs, ContainerEq(expected));
}

TEST_F(TestCaseOutputsTest, EmptyOutput) {
  TestCaseOutputs outputs;
  std::vector<std::string> parts = {
      "",
      "<TYPE A>\n",
  };

  FILE_BASED_TEST_DRIVER_ASSERT_OK(outputs.ParseFrom(parts));

  std::vector<std::string> actual_outputs;
  FILE_BASED_TEST_DRIVER_ASSERT_OK(outputs.GetCombinedOutputs(false /* include_possible_modes*/,
                                       &actual_outputs));
  EXPECT_THAT(actual_outputs, ContainerEq(parts));
}

TEST_F(TestCaseOutputsTest, ResultWithBrackets) {
  TestCaseOutputs outputs;
  std::vector<std::string> parts = {
      "[NOT A MODE]\ntest output",
  };

  FILE_BASED_TEST_DRIVER_ASSERT_OK(outputs.ParseFrom(parts));

  std::vector<std::string> actual_outputs;
  FILE_BASED_TEST_DRIVER_ASSERT_OK(outputs.GetCombinedOutputs(false /* include_possible_modes*/,
                                       &actual_outputs));
  EXPECT_THAT(actual_outputs, ContainerEq(parts));
}

TEST_F(TestCaseOutputsTest, EmptyTestMode) {
  TestCaseOutputs outputs;
  std::vector<std::string> parts = {
      "main test output",
      "<TYPE A>[]\ntest output",
  };

  EXPECT_THAT(
      outputs.ParseFrom(parts),
      StatusIs(_,
               HasSubstr("Found empty test mode enclosed in []:\n<TYPE A>[]")));
}

TEST_F(TestCaseOutputsTest, WhiteSpaces) {
  TestCaseOutputs outputs;
  std::vector<std::string> parts = {
      "<>  [MODE 1] [MODE_2]\ntest output line 1\ntest output line2",
      " <TYPE_A>  [MODE1]\ntest output 2\n",
  };

  std::vector<std::string> expected = {
      "<>[MODE 1][MODE_2]\ntest output line 1\ntest output line2",
      "<TYPE_A>[MODE1]\ntest output 2\n",
  };

  FILE_BASED_TEST_DRIVER_ASSERT_OK(outputs.ParseFrom(parts));

  std::vector<std::string> actual_outputs;
  FILE_BASED_TEST_DRIVER_ASSERT_OK(outputs.GetCombinedOutputs(false /* include_possible_modes*/,
                                       &actual_outputs));
  EXPECT_THAT(actual_outputs, ContainerEq(expected));
}

TEST_F(TestCaseOutputsTest, AddOutputTwice) {
  TestCaseOutputs outputs;
  std::vector<std::string> parts = {
      "main test output",
      "main test output",
  };

  EXPECT_THAT(
      outputs.ParseFrom(parts),
      StatusIs(
          _, HasSubstr("An output already exists for mode '', result type '':\n"
                       "first output:\nmain test output\nsecond output:\nmain "
                       "test output")));
}

TEST_F(TestCaseOutputsTest, AddDifferentOutput) {
  TestCaseOutputs outputs;
  std::vector<std::string> parts = {
      "<TYPE A>[MODE 1]\ntest output 1",
      "<TYPE A>[MODE 1]\ntest output 2",
  };

  EXPECT_THAT(
      outputs.ParseFrom(parts),
      StatusIs(
          _,
          HasSubstr(
              "An output already exists for mode 'MODE 1', result type 'TYPE "
              "A':\n"
              "first output:\ntest output 1\nsecond output:\ntest output 2")));
}

TEST_F(TestCaseOutputsTest, AllModesOutputExists) {
  TestCaseOutputs outputs;
  std::vector<std::string> parts = {
      "main test output",
      "<>[MODE 1]\nmode specific output",
  };

  EXPECT_THAT(outputs.ParseFrom(parts),
              StatusIs(_, HasSubstr("Cannot add output for mode 'MODE 1' "
                                    "and result type '' because an "
                                    "'all modes' output exists for the "
                                    "result type:\nall modes output:\n"
                                    "main test output")));
}

TEST_F(TestCaseOutputsTest, ModeSpecificOutputExists) {
  TestCaseOutputs outputs;
  std::vector<std::string> parts = {
      "<TYPE A>[MODE 1]\nmode 1 output",
      "<TYPE A>\nmain test output",
  };

  EXPECT_THAT(outputs.ParseFrom(parts),
              StatusIs(_, HasSubstr("Cannot add all modes output for result "
                                    "type 'TYPE A' because a 'MODE 1' "
                                    "output already exists for the result "
                                    "type\nmodes specific output:\n"
                                    "mode 1 output")));
}

TEST_F(TestCaseOutputsTest, ExtraTextAfterResultType) {
  TestCaseOutputs outputs;
  std::vector<std::string> parts = {
      "<TYPE A>some extra text",
  };

  EXPECT_THAT(outputs.ParseFrom(parts),
              StatusIs(_, HasSubstr("A test mode must be enclosed in [] but "
                                    "got: <TYPE A>some extra text")));
}

TEST_F(TestCaseOutputsTest, TestModeNotEnclosedInBrackets) {
  TestCaseOutputs outputs;
  std::vector<std::string> parts = {
      "<>[TEST MODE",
  };

  EXPECT_THAT(
      outputs.ParseFrom(parts),
      StatusIs(
          _, HasSubstr(
                 "A test mode must be enclosed in [] but got: <>[TEST MODE")));
}

TEST_F(TestCaseOutputsTest, MergeOneModeOutput) {
  TestCaseOutputs outputs;
  std::vector<std::string> parts = {
      "main test output",
  };

  FILE_BASED_TEST_DRIVER_ASSERT_OK(outputs.ParseFrom(parts));

  TestCaseOutputs to_merge;
  std::vector<std::string> to_merge_parts = {
      "<>[MODE 1]\nmain test output",
  };

  FILE_BASED_TEST_DRIVER_ASSERT_OK(to_merge.ParseFrom(to_merge_parts));

  TestCaseOutputs merged_outputs;
  FILE_BASED_TEST_DRIVER_ASSERT_OK(TestCaseOutputs::MergeOutputs(outputs, {to_merge},
                                          &merged_outputs));
  std::vector<std::string> actual_outputs;
  FILE_BASED_TEST_DRIVER_ASSERT_OK(merged_outputs.GetCombinedOutputs(false /* include_possible_modes*/,
                                              &actual_outputs));
  EXPECT_THAT(actual_outputs, ContainerEq(parts));
}

TEST_F(TestCaseOutputsTest, MergeMultipleModesOutput) {
  TestCaseOutputs outputs;
  std::vector<std::string> parts = {
      "<TYPE A>\ntest output",
  };

  FILE_BASED_TEST_DRIVER_ASSERT_OK(outputs.ParseFrom(parts));

  TestCaseOutputs to_merge;
  std::vector<std::string> to_merge_parts = {
      "<TYPE A>[MODE 1][MODE 2]\ntest output",
  };

  FILE_BASED_TEST_DRIVER_ASSERT_OK(to_merge.ParseFrom(to_merge_parts));

  TestCaseOutputs merged_outputs;
  FILE_BASED_TEST_DRIVER_ASSERT_OK(TestCaseOutputs::MergeOutputs(outputs, {to_merge},
                                          &merged_outputs));
  std::vector<std::string> actual_outputs;
  FILE_BASED_TEST_DRIVER_ASSERT_OK(merged_outputs.GetCombinedOutputs(false /* include_possible_modes*/,
                                              &actual_outputs));
  EXPECT_THAT(actual_outputs, ContainerEq(parts));
}

TEST_F(TestCaseOutputsTest, MergeWithExtraOutput) {
  TestCaseOutputs outputs;
  std::vector<std::string> parts = {
      "main test output",
  };

  FILE_BASED_TEST_DRIVER_ASSERT_OK(outputs.ParseFrom(parts));

  TestCaseOutputs to_merge1;
  std::vector<std::string> to_merge1_parts = {
      "<>[MODE 1]\nmain test output",
  };

  FILE_BASED_TEST_DRIVER_ASSERT_OK(to_merge1.ParseFrom(to_merge1_parts));

  TestCaseOutputs to_merge2;
  std::vector<std::string> to_merge2_parts = {
      "<>[MODE 2]\nmain test output",
      "<TYPE A>[MODE 2]\nanother output",
  };

  FILE_BASED_TEST_DRIVER_ASSERT_OK(to_merge2.ParseFrom(to_merge2_parts));

  TestCaseOutputs merged_outputs;
  FILE_BASED_TEST_DRIVER_ASSERT_OK(TestCaseOutputs::MergeOutputs(outputs, {to_merge1, to_merge2},
                                          &merged_outputs));
  std::vector<std::string> actual_outputs;
  FILE_BASED_TEST_DRIVER_ASSERT_OK(merged_outputs.GetCombinedOutputs(false /* include_possible_modes*/,
                                              &actual_outputs));
  std::vector<std::string> actual_outputs_str = {
      "main test output",
      "<TYPE A>[MODE 2]\nanother output",
  };

  EXPECT_THAT(actual_outputs, ContainerEq(actual_outputs_str));
}

TEST_F(TestCaseOutputsTest, MergeMissingOutput) {
  TestCaseOutputs outputs;
  std::vector<std::string> parts = {
      "main test output",
      "<TYPE A>[MODE 2]\nanother output",
  };

  FILE_BASED_TEST_DRIVER_ASSERT_OK(outputs.ParseFrom(parts));

  TestCaseOutputs to_merge1;
  std::vector<std::string> to_merge1_parts = {
      "<>[MODE 1]\nmain test output",
  };

  FILE_BASED_TEST_DRIVER_ASSERT_OK(to_merge1.ParseFrom(to_merge1_parts));

  TestCaseOutputs to_merge2;
  std::vector<std::string> to_merge2_parts = {
      "<TYPE A>[MODE 2]\nanother output",
  };

  FILE_BASED_TEST_DRIVER_ASSERT_OK(to_merge2.ParseFrom(to_merge2_parts));

  TestCaseOutputs merged_outputs;
  FILE_BASED_TEST_DRIVER_ASSERT_OK(TestCaseOutputs::MergeOutputs(outputs, {to_merge1, to_merge2},
                                          &merged_outputs));
  std::vector<std::string> actual_outputs;
  FILE_BASED_TEST_DRIVER_ASSERT_OK(merged_outputs.GetCombinedOutputs(false /* include_possible_modes*/,
                                              &actual_outputs));
  std::vector<std::string> actual_outputs_str = {
      "<>[MODE 1]\nmain test output",
      "<TYPE A>[MODE 2]\nanother output",
  };

  EXPECT_THAT(actual_outputs, ContainerEq(actual_outputs_str));
}

TEST_F(TestCaseOutputsTest, MergeDisabledMode) {
  TestCaseOutputs outputs;
  std::vector<std::string> parts = {
      "main test output",
      "<TYPE A>[MODE 2]\nanother output",
  };

  FILE_BASED_TEST_DRIVER_ASSERT_OK(outputs.ParseFrom(parts));

  TestCaseOutputs to_merge;
  std::vector<std::string> to_merge_parts = {
      "<>[MODE 1]\nmain test output",
  };

  FILE_BASED_TEST_DRIVER_ASSERT_OK(to_merge.ParseFrom(to_merge_parts));
  FILE_BASED_TEST_DRIVER_ASSERT_OK_AND_ASSIGN(TestCaseMode mode2, TestCaseMode::Create("MODE 2"));

  FILE_BASED_TEST_DRIVER_ASSERT_OK(to_merge.DisableTestMode(mode2));

  TestCaseOutputs merged_outputs;
  FILE_BASED_TEST_DRIVER_ASSERT_OK(TestCaseOutputs::MergeOutputs(outputs, {to_merge},
                                          &merged_outputs));
  std::vector<std::string> actual_outputs;
  FILE_BASED_TEST_DRIVER_ASSERT_OK(merged_outputs.GetCombinedOutputs(false /* include_possible_modes*/,
                                              &actual_outputs));
  std::vector<std::string> actual_outputs_str = {
      "main test output",
  };
  EXPECT_THAT(actual_outputs, ContainerEq(actual_outputs_str));
}

TEST_F(TestCaseOutputsTest, MergeIntoEmptyOutputs) {
  TestCaseOutputs outputs;

  TestCaseOutputs to_merge1;
  std::vector<std::string> to_merge1_parts = {
      "<>[MODE 1]\nmain test output",
  };

  FILE_BASED_TEST_DRIVER_ASSERT_OK(to_merge1.ParseFrom(to_merge1_parts));

  TestCaseOutputs to_merge2;
  std::vector<std::string> to_merge2_parts = {
      "<>[MODE 2]\nmain test output",
      "<TYPE A>[MODE 2]\nanother output",
  };

  FILE_BASED_TEST_DRIVER_ASSERT_OK(to_merge2.ParseFrom(to_merge2_parts));

  TestCaseOutputs merged_outputs;
  FILE_BASED_TEST_DRIVER_ASSERT_OK(TestCaseOutputs::MergeOutputs(outputs, {to_merge1, to_merge2},
                                          &merged_outputs));
  std::vector<std::string> actual_outputs;
  FILE_BASED_TEST_DRIVER_ASSERT_OK(merged_outputs.GetCombinedOutputs(false /* include_possible_modes*/,
                                              &actual_outputs));
  std::vector<std::string> actual_outputs_str = {
      "main test output",
      "<TYPE A>[MODE 2]\nanother output",
  };

  EXPECT_THAT(actual_outputs, ContainerEq(actual_outputs_str));
}

TEST_F(TestCaseOutputsTest, MergeInvalidPartialOutput) {
  TestCaseOutputs outputs;
  TestCaseOutputs to_merge;
  std::vector<std::string> to_merge_parts = {
      "main test output",
  };

  FILE_BASED_TEST_DRIVER_ASSERT_OK(to_merge.ParseFrom(to_merge_parts));

  TestCaseOutputs merged_outputs;
  EXPECT_THAT(
      TestCaseOutputs::MergeOutputs(outputs, {to_merge}, &merged_outputs),
      StatusIs(_, HasSubstr("Cannot merge partition output because it "
                            "contains 'all modes' result:\n"
                            "main test output")));
}

TEST_F(TestCaseOutputsTest, MergeEmptyOutputIntoAllModesOutput) {
  TestCaseOutputs outputs;
  std::vector<std::string> parts = {
      "main test output",
      "<TYPE A>\nTYPE A output",
  };

  FILE_BASED_TEST_DRIVER_ASSERT_OK(outputs.ParseFrom(parts));

  // Empty output
  TestCaseOutputs to_merge;

  TestCaseOutputs merged_outputs;
  FILE_BASED_TEST_DRIVER_ASSERT_OK(TestCaseOutputs::MergeOutputs(outputs, {to_merge},
                                          &merged_outputs));
  std::vector<std::string> actual_outputs;
  FILE_BASED_TEST_DRIVER_ASSERT_OK(merged_outputs.GetCombinedOutputs(false /* include_possible_modes*/,
                                              &actual_outputs));
  std::vector<std::string> actual_outputs_str = {
      "main test output",
      "<TYPE A>\nTYPE A output",
  };

  EXPECT_THAT(actual_outputs, ContainerEq(actual_outputs_str));
}

TEST_F(TestCaseOutputsTest,
       MergeEmptyOutputWithOneDisabledModeIntoModeSpecificOutput) {
  TestCaseOutputs outputs;
  std::vector<std::string> parts = {
      "main test output",
      "<TYPE A>[MODE 1]\nTYPE A output",
      "<TYPE A>[MODE 2]\nTYPE A output2",
  };

  FILE_BASED_TEST_DRIVER_ASSERT_OK(outputs.ParseFrom(parts));

  // Empty output
  TestCaseOutputs to_merge;
  FILE_BASED_TEST_DRIVER_ASSERT_OK_AND_ASSIGN(TestCaseMode mode1, TestCaseMode::Create("MODE 1"));
  FILE_BASED_TEST_DRIVER_ASSERT_OK(to_merge.DisableTestMode(mode1));

  TestCaseOutputs merged_outputs;
  FILE_BASED_TEST_DRIVER_ASSERT_OK(TestCaseOutputs::MergeOutputs(outputs, {to_merge},
                                          &merged_outputs));
  std::vector<std::string> actual_outputs;
  FILE_BASED_TEST_DRIVER_ASSERT_OK(merged_outputs.GetCombinedOutputs(false /* include_possible_modes*/,
                                              &actual_outputs));
  std::vector<std::string> actual_outputs_str = {
      "main test output",
      "<TYPE A>[MODE 2]\nTYPE A output2",
  };

  EXPECT_THAT(actual_outputs, ContainerEq(actual_outputs_str));
}

TEST_F(TestCaseOutputsTest,
       MergeEmptyOutputWithTwoDisabledModesIntoModeSpecificOutput) {
  TestCaseOutputs outputs;
  std::vector<std::string> parts = {
      "main test output",
      "<TYPE A>[MODE 1]\nTYPE A output",
      "<TYPE A>[MODE 2]\nTYPE A output2",
  };

  FILE_BASED_TEST_DRIVER_ASSERT_OK(outputs.ParseFrom(parts));

  TestCaseOutputs to_merge;
  FILE_BASED_TEST_DRIVER_ASSERT_OK_AND_ASSIGN(TestCaseMode mode1, TestCaseMode::Create("MODE 1"));
  FILE_BASED_TEST_DRIVER_ASSERT_OK_AND_ASSIGN(TestCaseMode mode2, TestCaseMode::Create("MODE 2"));
  FILE_BASED_TEST_DRIVER_ASSERT_OK(to_merge.DisableTestMode(mode1));
  FILE_BASED_TEST_DRIVER_ASSERT_OK(to_merge.DisableTestMode(mode2));

  TestCaseOutputs merged_outputs;
  FILE_BASED_TEST_DRIVER_ASSERT_OK(TestCaseOutputs::MergeOutputs(outputs, {to_merge},
                                          &merged_outputs));
  std::vector<std::string> actual_outputs;
  FILE_BASED_TEST_DRIVER_ASSERT_OK(merged_outputs.GetCombinedOutputs(false /* include_possible_modes*/,
                                              &actual_outputs));

  // We will keep the main output because there can be other test modes.
  std::vector<std::string> actual_outputs_str = {
      "main test output",
  };
  EXPECT_THAT(actual_outputs, ContainerEq(actual_outputs_str));
}

TEST_F(TestCaseOutputsTest, ParsePossibleModes) {
  TestCaseOutputs outputs;
  std::vector<std::string> parts = {
      "Possible Modes: [MODE_1][MODE_3][MODE_2]",
      "<>[MODE_1][MODE_2]\ntest output line 1\ntest output line2",
      "<TYPE_A>[MODE_1]\ntest output 2\n",
      "<TYPE B>\ntest output 3",
  };

  std::vector<std::string> expected = {
      "Possible Modes: [MODE_1][MODE_2][MODE_3]\n",
      "<>[MODE_1][MODE_2]\ntest output line 1\ntest output line2",
      "<TYPE B>\ntest output 3",
      "<TYPE_A>[MODE_1]\ntest output 2\n",
  };

  FILE_BASED_TEST_DRIVER_ASSERT_OK(outputs.ParseFrom(parts));

  std::vector<std::string> actual_outputs;
  FILE_BASED_TEST_DRIVER_ASSERT_OK(outputs.GetCombinedOutputs(true /* include_possible_modes*/,
                                       &actual_outputs));
  EXPECT_THAT(actual_outputs, ContainerEq(expected));
}

TEST_F(TestCaseOutputsTest, ParsePossibleModesFailed) {
  TestCaseOutputs outputs;
  std::vector<std::string> parts = {
      "Possible Modes: [MODE_1][MODE_3]",
      "<>[MODE_1][MODE_2]\ntest output line 1\ntest output line2",
      "<TYPE_A>[MODE_1]\ntest output 2\n",
      "<TYPE B>\ntest output 3",
  };

  EXPECT_THAT(outputs.ParseFrom(parts),
              StatusIs(_, HasSubstr("Cannot add output:\n"
                                    "test output line 1\ntest output line2\n"
                                    "for mode 'MODE_2' and result type ''\n"
                                    "because mode 'MODE_2' does not exist "
                                    "in the possible modes list: "
                                    "'MODE_1,MODE_3'")));
}

TEST_F(TestCaseOutputsTest, RecordOutputModeNotInPossibleModes) {
  TestCaseOutputs outputs;
  std::vector<std::string> parts = {
      "Possible Modes: [MODE_1][MODE_2][MODE_3]",
      "<>[MODE_1][MODE_2]\ntest output line 1\ntest output line2",
      "<TYPE_A>[MODE_1]\ntest output 2\n",
      "<TYPE B>\ntest output 3",
  };

  FILE_BASED_TEST_DRIVER_ASSERT_OK(outputs.ParseFrom(parts));
  FILE_BASED_TEST_DRIVER_ASSERT_OK_AND_ASSIGN(TestCaseMode mode4, TestCaseMode::Create("MODE_4"));
  EXPECT_THAT(
      outputs.RecordOutput(mode4, "TYPE C", "output 4"),
      StatusIs(_, HasSubstr("Cannot add output:\n"
                            "output 4\n\n"
                            "for mode 'MODE_4' and result type 'TYPE C'\n"
                            "because mode 'MODE_4' does not exist in the "
                            "possible modes list: "
                            "'MODE_1,MODE_2,MODE_3'")));
}

TEST_F(TestCaseOutputsTest, MergeOutputsWithPossibleModes) {
  TestCaseOutputs outputs;
  std::vector<std::string> parts = {"main test output"};

  FILE_BASED_TEST_DRIVER_ASSERT_OK(outputs.ParseFrom(parts));

  TestCaseOutputs to_merge;
  std::vector<std::string> to_merge_parts = {
      "<>[MODE 2]\nmain test output",
      "<TYPE A>[MODE 2]\nanother output",
  };

  FILE_BASED_TEST_DRIVER_ASSERT_OK(to_merge.ParseFrom(to_merge_parts));
  FILE_BASED_TEST_DRIVER_ASSERT_OK_AND_ASSIGN(TestCaseMode mode1, TestCaseMode::Create("MODE 1"));
  FILE_BASED_TEST_DRIVER_ASSERT_OK_AND_ASSIGN(TestCaseMode mode2, TestCaseMode::Create("MODE 2"));
  FILE_BASED_TEST_DRIVER_ASSERT_OK(to_merge.SetPossibleModes({mode1, mode2}));

  TestCaseOutputs merged_outputs;
  FILE_BASED_TEST_DRIVER_ASSERT_OK(TestCaseOutputs::MergeOutputs(outputs, {to_merge},
                                          &merged_outputs));

  std::vector<std::string> actual_outputs;
  FILE_BASED_TEST_DRIVER_ASSERT_OK(merged_outputs.GetCombinedOutputs(false /* include_possible_modes*/,
                                              &actual_outputs));
  std::vector<std::string> actual_outputs_str = {
      "main test output",
      "<TYPE A>[MODE 2]\nanother output",
  };

  EXPECT_THAT(actual_outputs, ContainerEq(actual_outputs_str));
}

TEST_F(TestCaseOutputsTest, PossibleModesDoNotIncludeAllModesInOutput) {
  TestCaseOutputs outputs;
  std::vector<std::string> parts = {
      "Possible Modes: [MODE_1][MODE_2][MODE_3]",
      "<>[MODE_1][MODE_2]\ntest output line 1\ntest output line2",
      "<TYPE_A>[MODE_1]\ntest output 2\n",
      "<TYPE B>\ntest output 3",
  };

  FILE_BASED_TEST_DRIVER_ASSERT_OK(outputs.ParseFrom(parts));

  FILE_BASED_TEST_DRIVER_ASSERT_OK_AND_ASSIGN(TestCaseMode mode1, TestCaseMode::Create("MODE_1"));
  FILE_BASED_TEST_DRIVER_ASSERT_OK_AND_ASSIGN(TestCaseMode mode3, TestCaseMode::Create("MODE_3"));
  EXPECT_THAT(
      outputs.SetPossibleModes({mode1, mode3}),
      StatusIs(_, HasSubstr("Cannot set possible modes to 'MODE_1,MODE_3' "
                            "because mode 'MODE_2' exists in the actual output "
                            "but does not exist in the possible modes.")));
}

}  // namespace file_based_test_driver

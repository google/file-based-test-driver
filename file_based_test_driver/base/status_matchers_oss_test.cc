//
// Copyright 2018 ZetaSQL Authors
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

#include "file_based_test_driver/base/status_matchers_oss.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "file_based_test_driver/base/status_macros.h"

namespace {

absl::StatusOr<int> OkStatusOr(int n) { return n; }

TEST(StatusMatcher, Macros) {
  FILE_BASED_TEST_DRIVER_EXPECT_OK(absl::OkStatus());
  FILE_BASED_TEST_DRIVER_ASSERT_OK(absl::OkStatus());
  FILE_BASED_TEST_DRIVER_ASSERT_OK_AND_ASSIGN(int value, OkStatusOr(15));
  EXPECT_EQ(value, 15);
}

}  // namespace

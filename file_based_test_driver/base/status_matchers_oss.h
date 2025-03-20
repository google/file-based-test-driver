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

#ifndef THIRD_PARTY_FILE_BASED_TEST_DRIVER_BASE_STATUS_MATCHERS_OSS_H_
#define THIRD_PARTY_FILE_BASED_TEST_DRIVER_BASE_STATUS_MATCHERS_OSS_H_

// Testing utilities for working with ::absl::Status and
// absl::StatusOr.
//
//
// Defines the following utilities:
//
//   =================
//   FILE_BASED_TEST_DRIVER_EXPECT_OK(s)
//
//   FILE_BASED_TEST_DRIVER_ASSERT_OK(s)
//   =================
//   Convenience macros for `EXPECT_THAT(s, IsOk())`, where `s` is either
//   a `Status` or a `StatusOr<T>` or a `StatusProto`.
//
//   There are no EXPECT_NOT_OK/ASSERT_NOT_OK macros since they would not
//   provide much value (when they fail, they would just print the OK status
//   which conveys no more information than EXPECT_FALSE(s.ok());
//   If you want to check for particular errors, better alternatives are:
//   EXPECT_THAT(s, StatusIs(expected_error));
//   EXPECT_THAT(s, StatusIs(_, HasSubstr("expected error")));
//
//   ===============
//   IsOkAndHolds(m)
//   ===============
//
//   This gMock matcher matches a StatusOr<T> value whose status is OK
//   and whose inner value matches matcher m.  Example:
//
//     using ::testing::MatchesRegex;
//     using ::testing::status::IsOkAndHolds;
//     ...
//     StatusOr<std::string> maybe_name = ...;
//     EXPECT_THAT(maybe_name, IsOkAndHolds(MatchesRegex("John .*")));
//
//   ===============================
//   StatusIs(status_code_matcher,
//            error_message_matcher)
//   ===============================
//
//   This gMock matcher matches a Status or StatusOr<T> or StatusProto value if
//   all of the following are true:
//
//     - the status' code() matches status_code_matcher, and
//     - the status' message() matches error_message_matcher.
//
//   Example:
//
//
//     using ::testing::HasSubstr;
//     using ::testing::MatchesRegex;
//     using ::testing::Ne;
//     using ::file_based_test_driver_base::testing::StatusIs;
//     using ::testing::_;
//     using absl::StatusOr;
//     StatusOr<std::string> GetName(int id);
//     ...
//
//     // The status code must be kAborted;
//     // the error message can be anything.
//     EXPECT_THAT(GetName(42),
//                 StatusIs(absl::StatusCode::kAborted, _));
//     // The status code can be anything; the error message must match the
//     // regex.
//     EXPECT_THAT(GetName(43),
//                 StatusIs(_, MatchesRegex("server.*time-out")));
//
//     // The status code should not be kAborted; the error message can be
//     // anything with "client" in it.
//     EXPECT_CALL(mock_env, HandleStatus(
//         StatusIs(Ne(absl::StatusCode::kAborted),
//                  HasSubstr("client"))));
//
//   ===============================
//   StatusIs(status_code_matcher)
//   ===============================
//
//   This is a shorthand for
//     StatusIs(status_code_matcher, testing::_)
//   In other words, it's like the two-argument StatusIs(), except that it
//   ignores error message.
//
//   ===============
//   IsOk()
//   ===============
//
//   Matches a absl::Status or file_based_test_driver_base::StatusOr<T> value
//   whose status value is StatusCode::kOK.
//   Equivalent to 'StatusIs(StatusCode::kOK)'.
//   Example:
//     using ::testing::status::IsOk;
//     ...
//     StatusOr<std::string> maybe_name = ...;
//     EXPECT_THAT(maybe_name, IsOk());
//     Status s = ...;
//     EXPECT_THAT(s, IsOk());
//

#include <ostream>
#include <string>
#include <type_traits>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "file_based_test_driver/base/source_location.h"
#include "file_based_test_driver/base/status_builder.h"

// Macros for testing the results of functions that return absl::Status or
// file_based_test_driver_base::StatusOr<T> (for any type T).
#define FILE_BASED_TEST_DRIVER_EXPECT_OK(expression) \
  EXPECT_THAT(expression, ::absl_testing::IsOk())
#define FILE_BASED_TEST_DRIVER_ASSERT_OK(expression) \
  ASSERT_THAT(expression, ::absl_testing::IsOk())

namespace file_based_test_driver {
namespace testing {
namespace internal_status {

void AddFatalFailure(
    absl::string_view expression,
    const ::file_based_test_driver_base::StatusBuilder& builder);

}  // namespace internal_status
}  // namespace testing
}  // namespace file_based_test_driver

// Executes an expression that returns a file_based_test_driver_base::StatusOr,
// and assigns the contained variable to lhs if the error code is OK. If the
// Status is non-OK, generates a test failure and returns from the current
// function, which must have a void return type.
//
// Example: Declaring and initializing a new value
//   FILE_BASED_TEST_DRIVER_ASSERT_OK_AND_ASSIGN(const ValueType& value,
//   MaybeGetValue(arg));
//
// Example: Assigning to an existing value
//   ValueType value;
//   FILE_BASED_TEST_DRIVER_ASSERT_OK_AND_ASSIGN(value, MaybeGetValue(arg));
//
// The value assignment example would expand into:
//   StatusOr<ValueType> status_or_value = MaybeGetValue(arg);
//   FILE_BASED_TEST_DRIVER_ASSERT_OK(statusor.status());
//   value = status_or_value.ConsumeValueOrDie();
//
// WARNING: Like FILE_BASED_TEST_DRIVER_ASSIGN_OR_RETURN, ASSERT_OK_AND_ASSIGN
// expands into
//   multiple statements; it cannot be used in a single statement (e.g. as the
//   body of an if statement without {})!
#define FILE_BASED_TEST_DRIVER_ASSERT_OK_AND_ASSIGN(lhs, rexpr)            \
  FILE_BASED_TEST_DRIVER_ASSIGN_OR_RETURN(                                 \
      lhs, rexpr,                                                          \
      ::file_based_test_driver::testing::internal_status::AddFatalFailure( \
          #rexpr, _))


#endif  // THIRD_PARTY_FILE_BASED_TEST_DRIVER_BASE_STATUS_MATCHERS_OSS_H_

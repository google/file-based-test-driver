#
# Copyright 2020 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

licenses(["notice"])  # Apache v2.0

cc_library(
    name = "base",
    hdrs = [
        "logging.h",
    ],
    tags = ["avoid_dep"],
    visibility = ["//file_based_test_driver:__pkg__"],
    deps = [
        ":logging",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/base:log_severity",
    ],
)

cc_library(
    name = "logging",
    srcs = ["logging.cc"],
    hdrs = ["logging.h"],
    tags = ["avoid_dep"],
    visibility = ["//file_based_test_driver:__pkg__"],
    deps = [
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/base:log_severity",
    ],
)

cc_library(
    name = "source_location",
    hdrs = ["source_location.h"],
    visibility = ["//file_based_test_driver:__pkg__"],
    deps = ["@com_google_absl//absl/base:config"],
)

cc_test(
    name = "source_location_test",
    srcs = ["source_location_test.cc"],
    deps = [
        ":source_location",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_library(
    name = "status",
    srcs = [
        "status_builder.cc",
        "statusor.cc",
    ],
    hdrs = [
        "status.h",
        "status_builder.h",
        "status_macros.h",
        "statusor.h",
        "statusor_internals.h",
    ],
    visibility = ["//file_based_test_driver:__pkg__"],
    deps = [
        ":logging",
        ":source_location",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/base:log_severity",
        "@com_google_absl//absl/meta:type_traits",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/utility",
    ],
)

cc_library(
    name = "ret_check",
    srcs = ["ret_check.cc"],
    hdrs = [
        "ret_check.h",
    ],
    visibility = ["//file_based_test_driver:__pkg__"],
    deps = [
        ":logging",
        ":source_location",
        ":status",
        "@com_google_absl//absl/status",
    ],
)

cc_test(
    name = "status_macros_test",
    srcs = ["status_macros_test.cc"],
    deps = [
        ":source_location",
        ":status",
        "@com_google_googletest//:gtest_main",
        "@com_google_absl//absl/memory",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "map_util",
    hdrs = [
        "map_traits.h",
        "map_util.h",
        "no_destructor.h",
    ],
    visibility = ["//file_based_test_driver:__pkg__"],
    deps = [
        ":logging",
        "@com_google_absl//absl/meta:type_traits",
    ],
)

cc_test(
    name = "map_util_unittest",
    srcs = [
        "map_util_test.h",
        "map_util_unittest.cc",
    ],
    deps = [
        ":logging",
        ":map_util",
        "@com_google_googletest//:gtest_main",
        "@com_google_absl//absl/container:node_hash_map",
    ],
)

cc_test(
    name = "no_destructor_test",
    size = "small",
    srcs = ["no_destructor_test.cc"],
    deps = [
        ":logging",
        ":map_util",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "map_traits_test",
    srcs = ["map_traits_test.cc"],
    deps = [
        ":map_util",
        "@com_google_googletest//:gtest_main",
        "@com_google_absl//absl/container:node_hash_map",
    ],
)

cc_test(
    name = "status_builder_test",
    srcs = ["status_builder_test.cc"],
    deps = [
        ":source_location",
        ":status",
        ":status_matchers_oss",
        "@com_google_googletest//:gtest_main",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
    ],
)

alias(
    name = "statusor",
    actual = ":status",
    visibility = ["//file_based_test_driver:__pkg__"],
)

cc_test(
    name = "statusor_test",
    size = "small",
    srcs = ["statusor_test.cc"],
    deps = [
        ":source_location",
        ":status",
        ":status_matchers_oss",
        "@com_google_googletest//:gtest_main",
        "@com_google_googletest//:gtest",
        "@com_google_absl//absl/memory",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
    ],
)

# This target exists to redefine status matcher symbols to a common (
# between oss and internal) place, namely, `file_based_test_driver::testing`.
# In the OSS release, this is replaced with a renamed status_matcher_oss.
cc_library(
    name = "status_matchers",
    testonly = 1,
    hdrs = ["status_matchers.h"],
    visibility = ["//file_based_test_driver:__pkg__"],
    deps = [
        ":status_matchers_oss",
        "@com_google_googletest//:gtest_main",
    ],
)

# This target exists to redefine unified diff symbols to a common (
# between oss and internal) place, namely, `file_based_test_driver::`.
# In the OSS release, this is replaced with a renamed unified_diff_oss.
cc_library(
    name = "unified_diff",
    testonly = 1,
    hdrs = ["unified_diff.h"],
    visibility = ["//file_based_test_driver:__pkg__"],
    deps = [
        ":unified_diff_oss",
    ],
)

cc_library(
    name = "unified_diff_oss",
    testonly = 1,
    srcs = [
        "diffchunk.cc",
        "lcs.cc",
        "lcs-inl.h",
        "lcs_hybrid.cc",
        "lcs_util.cc",
        "rediff.cc",
        "unified_diff_oss.cc",
    ],
    hdrs = [
        "diffchunk.h",
        "lcs.h",
        "lcs_hunt.h",
        "lcs_hybrid.h",
        "lcs_myers.h",
        "lcs_util.h",
        "rediff.h",
        "unified_diff_oss.h",
    ],
    deps = [
        "//file_based_test_driver/base:logging",
        "//file_based_test_driver/base:map_util",
        "@com_google_absl//absl/base",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/container:node_hash_map",
        "@com_google_absl//absl/random:bit_gen_ref",
        "@com_google_absl//absl/random:distributions",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:str_format",
        "@com_googlesource_code_re2//:re2",
    ],
)

cc_library(
    name = "lcs_test_util",
    testonly = 1,
    srcs = ["lcs_test_util.cc"],
    hdrs = ["lcs_test_util.h"],
    deps = [
        ":unified_diff_oss",
        "@com_google_googletest//:gtest",
        "@com_google_absl//absl/random:bit_gen_ref",
        "@com_google_absl//absl/random:distributions",
        "@com_google_absl//absl/strings",
    ],
)

cc_test(
    name = "lcs_test",
    srcs = ["lcs_test.cc"],
    deps = [
        ":lcs_test_util",
        ":unified_diff_oss",
        "//file_based_test_driver/base",
        "@com_google_googletest//:gtest_main",
        "@com_google_absl//absl/random",
        "@com_google_absl//absl/strings",
    ],
)

cc_test(
    name = "lcs_hunt_test",
    srcs = ["lcs_hunt_test.cc"],
    deps = [
        ":lcs_test_util",
        ":unified_diff_oss",
        "@com_google_googletest//:gtest_main",
        "@com_google_absl//absl/random",
    ],
)

cc_test(
    name = "lcs_hybrid_test",
    srcs = ["lcs_hybrid_test.cc"],
    deps = [
        ":lcs_test_util",
        ":unified_diff_oss",
        "@com_google_googletest//:gtest_main",
        "@com_google_absl//absl/random",
    ],
)

cc_test(
    name = "lcs_myers_test",
    srcs = ["lcs_myers_test.cc"],
    deps = [
        ":lcs_test_util",
        ":unified_diff_oss",
        "@com_google_googletest//:gtest_main",
        "@com_google_absl//absl/random",
    ],
)

cc_test(
    name = "lcs_util_test",
    srcs = ["lcs_util_test.cc"],
    deps = [
        ":unified_diff_oss",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "rediff_test",
    srcs = ["rediff_test.cc"],
    deps = [
        ":unified_diff_oss",
        "@com_google_googletest//:gtest_main",
        "@com_google_absl//absl/strings",
    ],
)

cc_test(
    name = "unified_diff_oss_test",
    srcs = ["unified_diff_oss_test.cc"],
    deps = [
        ":unified_diff_oss",
        "@com_google_googletest//:gtest_main",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "status_matchers_oss",
    testonly = 1,
    srcs = ["status_matchers_oss.cc"],
    hdrs = ["status_matchers_oss.h"],
    visibility = ["//file_based_test_driver:__pkg__"],
    deps = [
        ":source_location",
        ":status",
        "@com_google_googletest//:gtest",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
    ],
)

cc_test(
    name = "status_matchers_oss_test",
    size = "small",
    srcs = ["status_matchers_oss_test.cc"],
    deps = [
        ":status_matchers_oss",
        ":statusor",
        "@com_google_googletest//:gtest_main",
        "@com_google_googletest//:gtest",
    ],
)

cc_library(
    name = "path",
    testonly = 1,
    srcs = ["path.cc"],
    hdrs = ["path.h"],
    visibility = ["//file_based_test_driver:__pkg__"],
    deps = ["@com_google_absl//absl/strings"],
)

cc_test(
    name = "path_test",
    testonly = 1,
    srcs = ["path_test.cc"],
    visibility = ["//file_based_test_driver:__pkg__"],
    deps = [
        ":path",
        "@com_google_googletest//:gtest_main",
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "file_util",
    testonly = 1,
    hdrs = ["file_util.h"],
    visibility = ["//file_based_test_driver:__pkg__"],
    deps = [
        ":file_util_oss",
    ],
)

cc_library(
    name = "file_util_oss",
    testonly = 1,
    hdrs = ["file_util_oss.h"],
    deps = [
        ":logging",
        ":path",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
    ],
)

cc_test(
    name = "file_util_oss_test",
    testonly = 1,
    srcs = ["file_util_oss_test.cc"],
    data = ["file_util_oss_test.input_file"],
    deps = [
        ":file_util_oss",
        ":status_matchers",
        "@com_google_googletest//:gtest_main",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
    ],
)

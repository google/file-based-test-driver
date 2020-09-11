licenses(["notice"])

exports_files(["LICENSE"])

cc_library(
    name = "file_based_test_driver",
    testonly = 1,
    srcs = ["file_based_test_driver.cc"],
    hdrs = ["file_based_test_driver.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":alternations",
        ":run_test_case_result",
        "//base",
        # This appears necessary due to a bug in --config=no_modules
        "//base:logging",
        "@com_google_googletest//:gtest_main",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/functional:function_ref",
        "@com_google_absl//absl/memory",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/synchronization",
        "@com_google_absl//absl/time",
        "//base:file_util",
        "//base:unified_diff",
        "@com_googlesource_code_re2//:re2",
        "//base:status",
        "//base:ret_check",
    ],
)

cc_library(
    name = "test_case_options",
    testonly = 1,
    srcs = ["test_case_options.cc"],
    hdrs = ["test_case_options.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//base",
        # This appears necessary due to a bug in --config=no_modules
        "//base:logging",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/time",
        "//base:source_location",
        "//base:status",
        "//base:ret_check",
    ],
)

cc_library(
    name = "test_case_outputs",
    testonly = 1,
    srcs = ["test_case_outputs.cc"],
    hdrs = ["test_case_outputs.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":test_case_mode",
        "//base:map_util",
        "//base:ret_check",
        "//base:status",
        "//base:statusor",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/container:node_hash_map",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_googlesource_code_re2//:re2",
    ],
)

cc_library(
    name = "test_case_mode",
    testonly = 1,
    srcs = ["test_case_mode.cc"],
    hdrs = ["test_case_mode.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//base:status",
        "//base:statusor",
        "@com_google_absl//absl/container:node_hash_map",
        "@com_google_absl//absl/container:node_hash_set",
        "@com_google_absl//absl/hash",
        "@com_google_absl//absl/strings",
        "@com_googlesource_code_re2//:re2",
    ],
)

cc_test(
    name = "test_case_mode_test",
    srcs = ["test_case_mode_test.cc"],
    deps = [
        ":test_case_mode",
        "//base:status",
        "//base:status_matchers",
        "//base:statusor",
        "@com_google_googletest//:gtest_main",
        "@com_google_absl//absl/status",
    ],
)

cc_test(
    name = "test_case_options_test",
    size = "small",
    srcs = ["test_case_options_test.cc"],
    deps = [
        ":test_case_options",
        "//base:status_matchers",
        "@com_google_googletest//:gtest_main",
        "@com_google_absl//absl/time",
    ],
)

cc_test(
    name = "file_based_test_driver_test",
    size = "small",
    srcs = ["file_based_test_driver_test.cc"],
    deps = [
        ":file_based_test_driver",
        "//base",
        "//base:file_util",
        "//base:map_util",
        "//base:status_matchers",
        "@com_google_googletest//:gtest_main",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/functional:bind_front",
        "@com_google_absl//absl/strings",
    ],
)

cc_test(
    name = "example_test",
    size = "medium",
    srcs = ["example_test.cc"],
    # If any of the tests fail, provide per-test level failure message
    # rather than a global "Expected true".
    args = ["--file_based_test_driver_individual_tests=true"],
    data = ["example.test"],
    deps = [
        ":file_based_test_driver",
        ":run_test_case_result",
        ":test_case_options",
        "//base:path",
        "@com_google_googletest//:gtest_main",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/functional:bind_front",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
    ],
)

cc_test(
    name = "example_test_with_modes",
    size = "medium",
    srcs = ["example_test_with_modes.cc"],
    args = ["--file_based_test_driver_individual_tests=true"],
    data = ["example_with_modes.test"],
    shard_count = 1,
    deps = [
        ":file_based_test_driver",
        ":test_case_mode",
        ":test_case_options",
        "//base:path",
        "//base:status",
        "//base:status_matchers",
        "@com_google_googletest//:gtest_main",
        "@com_google_absl//absl/container:node_hash_map",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/functional:bind_front",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
    ],
)

cc_test(
    name = "test_case_outputs_test",
    size = "small",
    srcs = ["test_case_outputs_test.cc"],
    deps = [
        ":test_case_mode",
        ":test_case_outputs",
        "//base:status",
        "//base:status_matchers",
        "//base:statusor",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_library(
    name = "run_test_case_result",
    testonly = 1,
    hdrs = ["run_test_case_result.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":test_case_outputs",
    ],
)

cc_library(
    name = "alternations",
    testonly = 1,
    srcs = ["alternations.cc"],
    hdrs = ["alternations.h"],
    deps = [
        ":run_test_case_result",
        "//base:ret_check",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_googlesource_code_re2//:re2",
    ],
)

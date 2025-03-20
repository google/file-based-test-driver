package(
    default_visibility = ["//visibility:private"],
)

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
        ":test_case_mode",
        ":test_case_outputs",
        "//file_based_test_driver/base:file_util",
        "//file_based_test_driver/base:ret_check",
        "//file_based_test_driver/base:status",
        "//file_based_test_driver/base:unified_diff",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/base:nullability",
        "@com_google_absl//absl/cleanup",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/functional:function_ref",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/memory",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/synchronization",
        "@com_google_absl//absl/types:span",
        "@com_google_googletest//:gtest",
        "@com_googlesource_code_re2//:re2",
    ],
)

cc_library(
    name = "test_case_options",
    testonly = 1,
    srcs = ["test_case_options.cc"],
    hdrs = ["test_case_options.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//file_based_test_driver/base:ret_check",
        "//file_based_test_driver/base:source_location",
        "//file_based_test_driver/base:status",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/time",
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
        "//file_based_test_driver/base:map_util",
        "//file_based_test_driver/base:ret_check",
        "//file_based_test_driver/base:status",
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
        "//file_based_test_driver/base:status",
        "@com_google_absl//absl/container:node_hash_map",
        "@com_google_absl//absl/container:node_hash_set",
        "@com_google_absl//absl/hash",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_googlesource_code_re2//:re2",
    ],
)

cc_test(
    name = "test_case_mode_test",
    srcs = ["test_case_mode_test.cc"],
    deps = [
        ":test_case_mode",
        "//file_based_test_driver/base:status",
        "//file_based_test_driver/base:status_matchers",
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
        "//file_based_test_driver/base:status_matchers",
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
        ":run_test_case_result",
        ":test_case_mode",
        "//file_based_test_driver/base:file_util",
        "//file_based_test_driver/base:status_matchers",
        "@com_google_googletest//:gtest_main",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/functional:bind_front",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/log:log_entry",
        "@com_google_absl//absl/log:log_sink",
        "@com_google_absl//absl/log:log_sink_registry",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
    ],
)

cc_test(
    name = "example_test",
    size = "medium",
    srcs = ["example_test.cc"],
    data = ["example.test"],
    tags = ["notestify"],
    deps = [
        ":file_based_test_driver",
        ":run_test_case_result",
        ":test_case_options",
        "//file_based_test_driver/base:path",
        "@com_google_googletest//:gtest_main",
        "@com_google_absl//absl/functional:bind_front",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
    ],
)

cc_test(
    name = "parameterized_example_test",
    size = "medium",
    srcs = ["parameterized_example_test.cc"],
    data = ["example.test"],
    tags = ["notestify"],
    deps = [
        ":file_based_test_driver",
        ":run_test_case_result",
        ":test_case_options",
        "//file_based_test_driver/base:path",
        "@com_google_googletest//:gtest_main",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
    ],
)

cc_test(
    name = "sharded_example_test",
    size = "medium",
    srcs = ["sharded_example_test.cc"],
    data = ["example_no_alternations.test"],
    shard_count = 3,
    tags = ["notestify"],
    deps = [
        ":file_based_test_driver",
        ":run_test_case_result",
        ":test_case_options",
        "//file_based_test_driver/base:path",
        "@com_google_googletest//:gtest_main",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
    ],
)

cc_test(
    name = "example_test_with_modes",
    size = "medium",
    srcs = ["example_test_with_modes.cc"],
    data = ["example_with_modes.test"],
    shard_count = 1,
    tags = ["notestify"],
    deps = [
        ":file_based_test_driver",
        ":run_test_case_result",
        ":test_case_mode",
        ":test_case_options",
        "//file_based_test_driver/base:map_util",
        "//file_based_test_driver/base:path",
        "//file_based_test_driver/base:status_matchers",
        "@com_google_googletest//:gtest_main",
        "@com_google_absl//absl/container:node_hash_map",
        "@com_google_absl//absl/functional:bind_front",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
    ],
)

cc_test(
    name = "parameterized_example_test_with_modes",
    size = "medium",
    srcs = ["parameterized_example_test_with_modes.cc"],
    data = ["example_with_modes.test"],
    shard_count = 1,
    tags = ["notestify"],
    deps = [
        ":file_based_test_driver",
        ":run_test_case_result",
        ":test_case_mode",
        ":test_case_options",
        "//file_based_test_driver/base:map_util",
        "//file_based_test_driver/base:path",
        "//file_based_test_driver/base:status_matchers",
        "@com_google_googletest//:gtest_main",
        "@com_google_absl//absl/container:node_hash_map",
        "@com_google_absl//absl/functional:bind_front",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
    ],
)

cc_test(
    name = "sharded_example_test_with_modes",
    size = "medium",
    srcs = ["sharded_example_test_with_modes.cc"],
    data = ["example_with_modes_no_alternations.test"],
    shard_count = 3,
    tags = ["notestify"],
    deps = [
        ":file_based_test_driver",
        ":run_test_case_result",
        ":test_case_mode",
        ":test_case_options",
        "//file_based_test_driver/base:map_util",
        "//file_based_test_driver/base:path",
        "//file_based_test_driver/base:status_matchers",
        "@com_google_googletest//:gtest_main",
        "@com_google_absl//absl/container:node_hash_map",
        "@com_google_absl//absl/functional:bind_front",
        "@com_google_absl//absl/log:check",
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
        "//file_based_test_driver/base:status",
        "//file_based_test_driver/base:status_matchers",
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
        "//file_based_test_driver/base:ret_check",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_googlesource_code_re2//:re2",
    ],
)

#
# Copyright 2020 File Based Test DriverL Authors
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

""" Load dependencies. """

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

def file_based_test_driver_deps():
    """macro to include File Based Test Driver's dependencies in a WORKSPACE.
    """

    # Abseil
    if not native.existing_rule("com_google_absl"):
        # How to update:
        # Abseil generally just does daily (or even subdaily) releases. None are
        # special, so just periodically update as necessary.
        #
        #  https://github.com/abseil/abseil-cpp/commits/master
        #  pick a recent release.
        #  Hit the 'clipboard with a left arrow' icon to copy the commit hex
        #    COMMIT=<paste commit hex>
        #    PREFIX=abseil-cpp-
        #    REPO=https://github.com/abseil/abseil-cpp/archive
        #    URL=${REPO}/${COMMIT}.tar.gz
        #    wget $URL
        #    SHA256=$(sha256sum ${COMMIT}.tar.gz | cut -f1 -d' ')
        #    rm ${COMMIT}.tar.gz
        #    echo \# Commit from $(date --iso-8601=date)
        #    echo url = \"$URL\",
        #    echo sha256 = \"$SHA256\",
        #    echo strip_prefix = \"${PREFIX}${COMMIT}\",
        #
        http_archive(
            name = "com_google_absl",
            # Commit from 2020-03-03
            url = "https://github.com/abseil/abseil-cpp/archive/b19ba96766db08b1f32605cb4424a0e7ea0c7584.tar.gz",
            sha256 = "c7ff8decfbda0add222d44bdc27b47527ca4e76929291311474efe7354f663d3",
            strip_prefix = "abseil-cpp-b19ba96766db08b1f32605cb4424a0e7ea0c7584",
        )

    # GoogleTest/GoogleMock framework. Used by most unit-tests.
    if not native.existing_rule("com_google_googletest"):
        # How to update:
        # Googletest generally just does daily (or even subdaily) releases along
        # with occasional numbered releases.
        #
        #  https://github.com/google/googletest/commits/master
        #  pick a recent release.
        #  Hit the 'clipboard with a left arrow' icon to copy the commit hex
        #    COMMIT=<paste commit hex>
        #    PREFIX=googletest-
        #    REPO=https://github.com/google/googletest/archive/
        #    URL=${REPO}/${COMMIT}.tar.gz
        #    wget $URL
        #    SHA256=$(sha256sum ${COMMIT}.tar.gz | cut -f1 -d' ')
        #    rm ${COMMIT}.tar.gz
        #    echo \# Commit from $(date --iso-8601=date)
        #    echo url = \"$URL\",
        #    echo sha256 = \"$SHA256\",
        #    echo strip_prefix = \"${PREFIX}${COMMIT}\",
        #
        http_archive(
            name = "com_google_googletest",
            # Commit from 2020-02-21
            url = "https://github.com/google/googletest/archive//6f5fd0d7199b9a19faa9f499ecc266e6ae0329e7.tar.gz",
            sha256 = "51e6c4b4449aab8f31e69d0ff89565f49a1f3628a42e24f214e8b02b3526e3bc",
            strip_prefix = "googletest-6f5fd0d7199b9a19faa9f499ecc266e6ae0329e7",
        )

    # RE2 Regex Framework, mostly used in unit tests.
    if not native.existing_rule("com_googlesource_code_re2"):
        http_archive(
            name = "com_googlesource_code_re2",
            urls = [
                "https://github.com/google/re2/archive/d1394506654e0a19a92f3d8921e26f7c3f4de969.tar.gz",
            ],
            sha256 = "ac855fb93dfa6878f88bc1c399b9a2743fdfcb3dc24b94ea9a568a1c990b1212",
            strip_prefix = "re2-d1394506654e0a19a92f3d8921e26f7c3f4de969",
        )

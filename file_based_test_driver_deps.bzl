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
            # Commit from 2024-05-31
            url = "https://github.com/abseil/abseil-cpp/archive/d06b82773e2306a99a8971934fb5845d5c04a170.tar.gz",
            sha256 = "fd4c78078d160951f2317229511340f3e92344213bc145939995eea9ff9b9e48",
            strip_prefix = "abseil-cpp-d06b82773e2306a99a8971934fb5845d5c04a170",
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
            # Commit from 2022-11-15
            url = "https://github.com/google/googletest/archive/0e6aac2571eb1753b8855d8d1f592df64d1a4828.tar.gz",
            sha256 = "d1407f647bd6300b3434f7156fbf206100f8080b1661d8d56c57876c4173ddcd",
            strip_prefix = "googletest-0e6aac2571eb1753b8855d8d1f592df64d1a4828",
        )

    # RE2 Regex Framework, mostly used in unit tests.
    if not native.existing_rule("com_googlesource_code_re2"):
        # 2023-06-01
        http_archive(
            name = "com_googlesource_code_re2",
            urls = [
                "https://github.com/google/re2/archive/03da4fc0857c285e3a26782f6bc8931c4c950df4.tar.gz",
            ],
            sha256 = "ef516fb84824a597c4d5d0d6d330daedb18363b5a99eda87d027e6bdd9cba299",
            strip_prefix = "re2-03da4fc0857c285e3a26782f6bc8931c4c950df4",
        )

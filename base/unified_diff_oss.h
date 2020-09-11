//
// Copyright 2012 Google LLC
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

#ifndef THIRD_PARTY_FILE_BASED_TEST_DRIVER_BASE_UNIFIED_DIFF_OSS_H_
#define THIRD_PARTY_FILE_BASED_TEST_DRIVER_BASE_UNIFIED_DIFF_OSS_H_

#include <functional>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"

namespace file_based_test_driver_base {

class UnifiedDiffColorizer {
 public:
  // The prefix and suffix strings are applied when highlighting an added or
  // deleted blocks of text.
  UnifiedDiffColorizer(absl::string_view add_prefix,
                       absl::string_view add_suffix,
                       absl::string_view del_prefix,
                       absl::string_view del_suffix,
                       std::function<std::string(absl::string_view)> escaper)
      : add_prefix_(add_prefix),
        add_suffix_(add_suffix),
        del_prefix_(del_prefix),
        del_suffix_(del_suffix),
        escaper_(std::move(escaper)) {}
  UnifiedDiffColorizer(const UnifiedDiffColorizer&) = delete;
  UnifiedDiffColorizer& operator=(const UnifiedDiffColorizer&) = delete;

  const std::string& add_prefix() const { return add_prefix_; }
  const std::string& add_suffix() const { return add_suffix_; }
  const std::string& del_prefix() const { return del_prefix_; }
  const std::string& del_suffix() const { return del_suffix_; }
  const std::function<std::string(absl::string_view)>& escaper() const {
    return escaper_;
  }

  // Standard colorizer using ANSI terminal escape codes.
  static const UnifiedDiffColorizer* AnsiColorizer();

 private:
  const std::string add_prefix_, add_suffix_, del_prefix_, del_suffix_;
  const std::function<std::string(absl::string_view)> escaper_;
};

class UnifiedDiffOptions {
 public:
  UnifiedDiffOptions()
      : context_size_(3),
        warn_missing_eof_newline_(true),
        colorizer_(nullptr) {}

  const UnifiedDiffColorizer* colorizer() const { return colorizer_; }
  // Does not take ownership, must exist throughout lifetime of the
  // UnifiedDiffOptions.
  UnifiedDiffOptions& set_colorizer(const UnifiedDiffColorizer* colorizer) {
    colorizer_ = colorizer;
    return *this;
  }

  int context_size() const { return context_size_; }
  UnifiedDiffOptions set_context_size(const int context_size) {
    context_size_ = context_size;
    return *this;
  }

  bool warn_missing_eof_newline() const { return warn_missing_eof_newline_; }
  UnifiedDiffOptions& set_warn_missing_eof_newline(
      const bool warn_missing_eof_newline) {
    warn_missing_eof_newline_ = warn_missing_eof_newline;
    return *this;
  }

 private:
  int context_size_;
  bool warn_missing_eof_newline_;
  const UnifiedDiffColorizer* colorizer_;
};

// Returns the unified line-by-line diff between the contents of left
// and right. left and right will not be copied.
//
// left_name and right_name are used as the file names in the diff headers,
// context_size has the same meaning as the -u X argument to diff.
//
// If left and right are identical, return the empty string.
std::string UnifiedDiff(absl::string_view left, absl::string_view right,
                        absl::string_view left_name,
                        absl::string_view right_name,
                        const UnifiedDiffOptions& options);

}  // namespace file_based_test_driver_base

#endif  // THIRD_PARTY_FILE_BASED_TEST_DRIVER_BASE_UNIFIED_DIFF_OSS_H_

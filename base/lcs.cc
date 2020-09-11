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

#include "base/lcs.h"

#include <vector>

#include "absl/base/casts.h"
#include <cstdint>
#include "absl/strings/string_view.h"
#include "base/lcs_hybrid.h"

namespace file_based_test_driver_base {

void Lcs::set_options(const LcsOptions& options) {
  options_ = options;
}

LcsOptions* Lcs::mutable_options() {
  return &options_;
}

int Lcs::Run(const std::vector<int>& left, const std::vector<int>& right,
             std::vector<Chunk>* chunks) {
  return Run(left.data(), left.size(), right.data(), right.size(), chunks);
}

int Lcs::Run(const int* left, int left_size, const int* right, int right_size,
             std::vector<Chunk>* chunks) {
  internal::LcsHybrid<int> hybrid;
  hybrid.set_options(options_);
  return hybrid.Run(left, left_size, 0, right, right_size, 0, chunks);
}

int Lcs::Run(absl::string_view left, absl::string_view right,
             std::vector<Chunk>* chunks) {
  internal::LcsHybrid<uint8_t> hybrid;
  LcsOptions* opt = hybrid.mutable_options();
  *opt = options_;
  // Note: C++ strings may contain null characters.
  opt->set_max_keys(std::numeric_limits<uint8_t>::max() + 1);
  return hybrid.Run(absl::bit_cast<const uint8_t*>(left.data()), left.size(), 0,
                    absl::bit_cast<const uint8_t*>(right.data()), right.size(), 0,
                    chunks);
}

}  // namespace file_based_test_driver_base

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

#include "base/lcs_hybrid.h"

#include <algorithm>

#include "base/logging.h"
#include <cstdint>
#include "base/lcs.h"
#include "base/lcs_hunt.h"

namespace file_based_test_driver_base {
namespace internal {

void LcsStats::LcsBounds(int* lower_bound, int* upper_bound) const {
  if (gamma_) {
    int max_lcs = std::min(left_size_, right_size_);
    //           beta_ <= left_size_ * right_size_
    // ==> lower_bound <= min(left_size_, right_size_)
    *lower_bound = beta_ / (left_size_ + right_size_);
    // Proof that *upper_bound >= *lower_bound:
    // *upper_bound = beta_ * used_keys_ / gamma_ >=        [ used_keys_ >= 1 ]
    // beta_ / gamma_ >=        [ gamma_ <= std::min(left_size_, right_size_) ]
    // beta_ / std::min(left_size_, right_size_) >=
    // beta_ / (left_size_ + right_size) == *lower_bound
    *upper_bound = std::min<double>(1.0 * beta_ * used_keys_ / gamma_, max_lcs);
    // upper_bound can only be smaller than lower_bound due to precision issues,
    // which we want to ignore here.
    *upper_bound = std::max(*upper_bound, *lower_bound);
  } else {
    *lower_bound = 0;
    *upper_bound = 0;
  }
}

void LcsStats::DiffBounds(int* lower_bound, int* upper_bound) const {
  int lcs_upper, lcs_lower;
  LcsBounds(&lcs_lower, &lcs_upper);
  // Just transform the length of longest common subsequence to the difference.
  *upper_bound = left_size_ + right_size_ - lcs_lower * 2;
  *lower_bound = left_size_ + right_size_ - lcs_upper * 2;
  CHECK_LE(*lower_bound, *upper_bound);
}

static float sqr(float a) {
  return a * a;
}

LcsHybridEstimator::LcsHybridEstimator(int left_size, int right_size,
                                       const LcsOptions* options)
    : left_size_(left_size),
      right_size_(right_size),
      options_(options),
      stats_(nullptr) {}

float LcsHybridEstimator::MyersWorstCaseRuntime() const {
  return sqr(left_size_ + right_size_) * options_->myers_factor();
}

// Returns the memory consumption of Myers' algorithm for the backpointer
// version. The input parameter specifies the maximum diff.
static int64_t MyersMemoryConsumption(int kMaxDiff) {
  int64_t kMax = (kMaxDiff + 1LL) / 2;
  return (kMax + 2LL) * (kMax + 1LL) * sizeof(int);
}

int64_t LcsHybridEstimator::MyersWorstCaseMemory() const {
  return MyersMemoryConsumption(left_size_ + right_size_);
}

int64_t LcsHybridEstimator::MyersSplitMemory() const {
  return (2LL + left_size_ + right_size_) * sizeof(int);
}

float LcsHybridEstimator::HuntsBestCaseRuntime() const {
  int keys = options_->max_keys();
  // For the best case, we assume that each key occurs with the same
  // probability. The worst case happens if a single key occurs almost
  // everywhere. In fact, the best case would be if the sequences don't share
  // any key. But this is an too optimistic assumption.
  float min_beta = static_cast<float>(right_size_) * left_size_ / keys;
  // Besides the time for running hunts algorithm, we need to spend time for
  // initializing the KeyOccurrences data structure. For simplicity,
  // we also initialize the LcsStats in such a case, since it is not
  // contributing that much anymore.
  return options_->init_factor() * right_size_ +
      options_->estimate_factor() * left_size_ +
      options_->hunt_factor() * min_beta;
}

int64_t LcsHybridEstimator::HuntsSplitMemory() const {
  return right_size_ * 4LL * sizeof(int) + KeyOccurrencesMemory();
}

int64_t LcsHybridEstimator::KeyOccurrencesMemory() const {
  return (options_->max_keys() + 1LL + right_size_) * sizeof(int);
}

int64_t LcsHybridEstimator::InitMemory() const {
  return KeyOccurrencesMemory() + (options_->max_keys() + 1LL) * sizeof(int);
}

void LcsHybridEstimator::set_stats(const LcsStats* stats) {
  stats_ = stats;
}

float LcsHybridEstimator::MyersRuntime() const {
  int lower_bound, upper_bound;
  stats_->DiffBounds(&lower_bound, &upper_bound);
  float weighted_bound =
      lower_bound * options_->lcs_bound_ratio() +
      upper_bound * (1.f - options_->lcs_bound_ratio());
  // In our experiments, the runtime was quite well modelled by the number of
  // executions of the inner-most for loop. For some corner cases, the search
  // for the longest common prefix at a certain position might become a dominant
  // factor. With the aid of SuffixArrays the complexity of this step can be
  // reduced from a linear to a logarithmic factor in the worst-case.
  return sqr(weighted_bound) * options_->myers_factor();
}

float LcsHybridEstimator::HuntsRuntime() const {
  // We model the runtime only by the number of times, the inner-most for loop
  // is executed and ignore that the complexity of the binary search depends on
  // the input size.
  return stats_->beta() * options_->hunt_factor();
}

int64_t LcsHybridEstimator::HuntsMemory() const {
  // In the worst case, the inner-most loop writes a new backpointer every time.
  // Since the inner-most loop is executed beta() times, we get:
  return stats_->beta() * sizeof(BackPointer) +
      right_size_ * sizeof(int) +
      KeyOccurrencesMemory();
}

int64_t LcsHybridEstimator::MyersMemory() const {
  int lower_bound, upper_bound;
  stats_->DiffBounds(&lower_bound, &upper_bound);
  return MyersMemoryConsumption(upper_bound);
}

int64_t LcsHybridEstimator::GetMemoryRecommendation() const {
  // Hunts memory consumption consists of an initialization phase and the actual
  // LCS computation. The maximum of both phases gives the required memory.
  int64_t min_hunts_memory = std::max(InitMemory(), HuntsSplitMemory());
  // Since both algorithms require linear memory in their split version, we
  // return the maximum memory, such that later on the faster algorithm is
  // picked.
  return std::max(min_hunts_memory, MyersSplitMemory());
}

}  // namespace internal
}  // namespace file_based_test_driver_base

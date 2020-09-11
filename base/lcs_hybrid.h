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

//
// LcsHybrid chooses between LcsMyers and LcsHunt implementation depending
// on estimates for their runtimes. It uses either algorithm in a recursive
// implementation in order to get linear memory consumption if the memory limit
// would be exceeded otherwise.
#ifndef THIRD_PARTY_FILE_BASED_TEST_DRIVER_BASE_LCS_HYBRID_H_
#define THIRD_PARTY_FILE_BASED_TEST_DRIVER_BASE_LCS_HYBRID_H_

#include <algorithm>
#include <vector>

#include "base/logging.h"
#include <cstdint>
#include "base/lcs.h"
#include "base/lcs_hunt.h"
#include "base/lcs_myers.h"
#include "base/lcs_util.h"

namespace file_based_test_driver_base {
namespace internal {

// The derivation of the upper and lower bound formulas for Longest Common
// Subsequences are described in the paper
// "Fast Algorithms to Approximate LCS Length" from M. Pal and M. Bansal.
//
// The statistical variables are named as in the paper.
class LcsStats {
 public:
  // Extracts some basic statistical information about the two provided
  // sequences. For of efficiency reasons, one of the two must be available
  // as KeyOccurrences data structure.
  // The computational complexity of the initialization procedure is
  // O(left_size + right_size) consuming O(right.max_keys) additional memory.
  template <class Item>
  LcsStats(const Item* left, int left_size, const KeyOccurrences& right);

  // Computes upper and lower bounds for the longest common subsequence problem.
  void LcsBounds(int* lower_bound, int* upper_bound) const;

  // Computes upper and lower bounds for the difference of the two sequences.
  void DiffBounds(int* lower_bound, int* upper_bound) const;

  // Returns: sum_k occs_left[k] * occs_right[k].
  int64_t beta() const { return beta_; }

  // Returns: sum_k min(occs_left[k], occs_right[k])
  int gamma() const { return gamma_; }

  // Returns the number of keys which occur in both sequences, i.e.
  //   sum_k min(occs_left[k], occs_right[k], 1)
  int keys() const { return used_keys_; }

 private:
  int left_size_;
  int right_size_;
  int gamma_;
  int64_t beta_;
  int used_keys_;
};

// Estimates the runtime and memory consumption for Myers' and Hunt's
// algorithm. We ignore the memory consumed by the output chunk vector.
class LcsHybridEstimator {
 public:
  // The passed in options instance has to outlive this estimator instance.
  LcsHybridEstimator(int left_size, int right_size,
                     const LcsOptions* options);

  // The subsequent functions require only left_size and right_size being set.
  float MyersWorstCaseRuntime() const;
  int64_t MyersWorstCaseMemory() const;
  int64_t MyersSplitMemory() const;

  // The subsequent functions rely on max_keys being set in the passed options.
  float HuntsBestCaseRuntime() const;
  int64_t HuntsSplitMemory() const;
  int64_t KeyOccurrencesMemory() const;
  int64_t InitMemory() const;

  // Returns a memory recommendation which is linear in the input size and
  // provides enough space such that the split versions of Hunt's and Myers'
  // algorithm can run within this limit.
  int64_t GetMemoryRecommendation() const;

  // The passed in stats instance must exist while the below functions are
  // called.
  void set_stats(const LcsStats* stats);

  // All the below functions require that set_stats has been called with a
  // well initialized LcsStats instance.
  float MyersRuntime() const;
  float HuntsRuntime() const;
  int64_t MyersMemory() const;
  int64_t HuntsMemory() const;

 private:
  // Avoid numeric overflow by using int64_t instead of int32_t.
  int64_t left_size_;
  int64_t right_size_;
  const LcsOptions* options_;
  const LcsStats* stats_;
};

// Combines Myers' and Hunt's algorithm into one hybrid version which estimates
// the runtime for both algorithms and chooses the faster one.
// Also it tries to reduce memory consumption if necessary by switching to the
// recursive version of each algorithm which only consumes memory linear in the
// size of the input.
template <class ItemT>
class LcsHybrid {
 public:
  typedef ItemT Item;

  void set_options(const LcsOptions& options);
  LcsOptions* mutable_options();

  int Run(const Item* left, int left_size, int left_offset,
          const Item* right, int right_size, int right_offset,
          std::vector<Chunk>* chunks);

 private:
  int RunHybrid(const Item* left, int left_size, int left_offset,
                const Item* right, int right_size, int right_offset,
                std::vector<Chunk>* chunks);

  LcsOptions options_;
  KeyOccurrences right_occ_;
  LcsMyers<Item> myers_;
  LcsHunt<Item> hunt_;
};

template <class Item>
LcsStats::LcsStats(const Item* left, int left_size,
                   const KeyOccurrences& right)
    : left_size_(left_size),
      right_size_(right.size()),
      gamma_(0),  // Computes: sum_k min(occs_left[k], occs_right[k])
      beta_(0),  // Computes: sum_k occs_left[k] * occs_right[k].
      used_keys_(0) {  // Counts unique keys which occur on both sides.
  const std::vector<int>& first_match = right.first_match_;
  // Keep track of already consumed items of the right side.
  std::vector<int> consumed_matches(first_match);
  for (int i = 0; i < left_size; i++) {
    int k = left[i];
    // Only the first usage of a key is of interest for counting unique keys.
    if (consumed_matches[k] == first_match[k])
      used_keys_++;
    // Compute the product occs_left[k] * occs_right[k] incrementally.
    beta_ += first_match[k + 1] - first_match[k];
    // Compute min of occs_left[k] and occs_right[k] incrementally.
    if (consumed_matches[k] < first_match[k + 1])
      gamma_++;
    // One item on the right side has been consumed.
    ++consumed_matches[k];
  }
}

template <class ItemT>
void LcsHybrid<ItemT>::set_options(const LcsOptions& options) {
  options_ = options;
}

template <class ItemT>
LcsOptions* LcsHybrid<ItemT>::mutable_options() {
  return &options_;
}

template <class ItemT>
int LcsHybrid<ItemT>::Run(const Item* left, int left_size, int left_offset,
                          const Item* right, int right_size, int right_offset,
                          std::vector<Chunk>* chunks) {
  // Consume leading matches
  int leading_matches = 0;
  while (leading_matches < std::min(left_size, right_size) &&
         left[leading_matches] == right[leading_matches])
    leading_matches++;
  left_size -= leading_matches;
  right_size -= leading_matches;
  left += leading_matches;
  right += leading_matches;

  if (leading_matches && chunks)
    AppendChunk(left_offset, right_offset, leading_matches, chunks);
  left_offset += leading_matches;
  right_offset += leading_matches;

  // Consume trailing matches
  int trailing_matches = 0;
  while (std::min(left_size, right_size) > 0 &&
         left[left_size - 1] == right[right_size - 1]) {
    trailing_matches++;
    left_size--;
    right_size--;
  }

  // Process the rest of the input
  int lcs = RunHybrid(left, left_size, left_offset,
                      right, right_size, right_offset,
                      chunks);
  if (lcs < 0)  // Handle error cases.
    return lcs;

  // And finally append the trailing chunk.
  if (trailing_matches && chunks)
    AppendChunk(left_offset + left_size, right_offset + right_size,
                trailing_matches, chunks);

  return lcs + leading_matches + trailing_matches;
}

template <class ItemT>
int LcsHybrid<ItemT>::RunHybrid(
    const Item* left, int left_size, int left_offset,
    const Item* right, int right_size, int right_offset,
    std::vector<Chunk>* chunks) {
  if (left_size == 0 || right_size == 0)
    return 0;  // Handle trivial cases

  bool use_hunt;
  int64_t memory_consumption;
  // Avoid computation of statistics, if we know from the very beginning
  // that Myers' algorithm will be faster.
  LcsHybridEstimator estimator(left_size, right_size, &options_);
  if (estimator.MyersWorstCaseRuntime() <= estimator.HuntsBestCaseRuntime() &&
      estimator.MyersWorstCaseMemory() <= options_.max_memory()) {
    use_hunt = false;
    memory_consumption = estimator.MyersWorstCaseMemory();
  } else if (estimator.InitMemory() > options_.max_memory()) {
    // We cannot even initialize some basic data structures required by Hunt's
    // algorithm. Thus, give Myers' algorithm a try.
    use_hunt = false;
    memory_consumption = estimator.MyersWorstCaseMemory();
  } else {
    // Check memory constraints before initializing helper structures.
    if (estimator.InitMemory() > options_.max_memory())
      return kLcsMemoryLimitExceeded;  // Memory exceeded!

    right_occ_.Init(right, right_size, options_.max_keys());
    LcsStats stats(left, left_size, right_occ_);
    estimator.set_stats(&stats);
    // Choose the faster version. Myers' algorithm is more memory efficient.
    // So, choose Myers' if Hunt's algorithm doesn't fit even in the recursive
    // version.
    use_hunt = (estimator.HuntsRuntime() < estimator.MyersRuntime() &&
                estimator.HuntsSplitMemory() <= options_.max_memory());
    memory_consumption =
        use_hunt ? estimator.HuntsMemory() : estimator.MyersMemory();
    estimator.set_stats(nullptr);
  }

  std::vector<Chunk>* chunks_arg = chunks;
  if (memory_consumption > options_.max_memory()) {
    // The non-recursive solution exceeds the given memory constraint. Thus
    // switch to the recursive solution using a split point.
    chunks_arg = nullptr;
    memory_consumption =
        use_hunt ? estimator.HuntsSplitMemory() : estimator.MyersSplitMemory();
    // Does it fit into memory now?
    if (memory_consumption > options_.max_memory())
      return kLcsMemoryLimitExceeded;  // Memory exceeded!
  }

  int lcs = 0;
  if (use_hunt) {
    lcs = hunt_.Run(left, left_size, left_offset,
                    right_occ_, right_offset, chunks_arg);
  } else {
    // TODO Free memory of stats_ and right_occ_.
    lcs = myers_.Run(left, left_size, left_offset, right, right_size,
                     right_offset, chunks_arg);
  }
  if (chunks_arg != chunks && lcs > 0) {
    // Recursively solve the LCS which results in a linear memory consumption
    // without increasing the computational complexity.
    int split_x = use_hunt ? hunt_.split_x() : myers_.split_x();
    int split_y = use_hunt ? hunt_.split_y() : myers_.split_y();
    int a = Run(left, split_x, left_offset, right, split_y, right_offset,
                chunks);
    int b = Run(left + split_x, left_size - split_x, left_offset + split_x,
                right + split_y, right_size - split_y, right_offset + split_y,
                chunks);
    DCHECK(a + b == lcs);
  }
  return lcs;
}

}  // namespace internal
}  // namespace file_based_test_driver_base

#endif  // THIRD_PARTY_FILE_BASED_TEST_DRIVER_BASE_LCS_HYBRID_H_

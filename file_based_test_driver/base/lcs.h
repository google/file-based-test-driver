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

// API for Longest Common Subsequence computations.
// Note: Don't mistake it for Longest Common Substring!
//
// The following example demonstrates how to use the Lcs class:
//
//   vector<string> left_lines, right_lines;
//   ... // Fill in some lines to left_lines and right_lines.
//
//   vector<int> left_int, right_int;
//   // Convert arbitrary input into integer representation.
//   int keys = Lcs::MapToInteger<absl::string_view>(left_lines, right_lines,
//                                                   &left_int, &right_int);
//   Lcs lcs;
//   // For performance reasons tell the LCS algorithm, how many
//   // different keys (i.e. distinct lines) the input has. This is an optional
//   // step.
//   lcs.mutable_options()->set_max_keys(keys);
//   vector<Chunk> chunks;
//   lcs.Run(left_int, right_int, &chunks);
//
// The MapToInteger function has been introduced to reduce the amount
// of templates being included by the Lcs API. This function
// allows conversion of most input formats into a standard vector<int>
// representation. The Lcs algorithms themselves are templated such that
// Lcs computations can be done on either strings or ints without the need
// for conversion in these cases.

#ifndef THIRD_PARTY_FILE_BASED_TEST_DRIVER_BASE_LCS_H__
#define THIRD_PARTY_FILE_BASED_TEST_DRIVER_BASE_LCS_H__

#include <memory>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/strings/string_view.h"
#include "file_based_test_driver/base/map_util.h"

namespace file_based_test_driver_base {

// Since LCS have always a non-negative length, negative values are used for
// expressing errors.
enum LcsErrorCodes {
  kLcsMemoryLimitExceeded = -1,
  // TODO Use this to control runtime of Myers' algorithm.
  kLcsMaxDiffExceeded     = -3,
};

// Configuration of the Lcs algorithms is done via LcsOptions.
class LcsOptions {
 public:
  LcsOptions();

  // Determine the maximum amount of memory which should be allocated by the
  // LCS instance. If the memory is not sufficient for computing LCS, the
  // status field will be set to INSUFFICIENT_MEMORY and no matches will be
  // reported.
  // Internally, the algorithm switches between recursive and backpointer
  // implementations. The former consume only a linear amount of memory
  // but have a higher constant than the latter. However, the latter consume
  // a quadratic amount memory in the worst case.
  int max_memory() const { return max_memory_; }
  // The number of distinct keys is crucial for the runtime of certain LCS
  // algorithms. max_keys specifies an upper (exclusive) bound to integer
  // keys and therewith an upper bound to the number of distinct keys.
  // max_keys set to MAX_INT indicates that the upper bound is unknown.
  int max_keys() const { return max_keys_; }
  // Depending on the input sequences, Myers' or Hunt's algorithm will be
  // faster. However, the runtime of Myers' algorithm depends on the number
  // of differences between the two sequences which can only be efficiently
  // bound by lower and upper limits. In certain cases, these limits can
  // be far apart. A pessimistic estimate based on the upper bound will
  // unneccesarily worsen the overall runtime of LCS. Instead, we weight
  // lower and upper bound by lcs_bound_ratio in the following way:
  //   lcs_bound_ratio * lower_bound + (1 - lcs_bound_ratio) * upper_bound
  // The default value underestimates the runtime of Myers' algorithm by
  // about a factor 4.
  float lcs_bound_ratio() const { return lcs_bound_ratio_; }
  // This factor is used to estimate the runtime of Hunt's algorithm which
  // is proportional to the sum_i c_i(left) * c_i(right), where c_i(x)
  // corresponds to the number of occurrences of symbol i in sequence x.
  float hunt_factor() const { return hunt_factor_; }
  // This factor is used to estimate the runtime of Myers' algorithm which
  // is in theory proportional to (n + m) * difference, where n and m
  // are the sizes of the two input sequences and difference is the number
  // of not matching characters. However, in practice the runtime is usually
  // proportional to difference^2. For input sequences sharing a long
  // repetitive subsequence, the runtime might be underestimated. However,
  // in such cases also Hunt's algorithm will usually perform even worse.
  float myers_factor() const { return myers_factor_; }
  // This factor is used to estimate the runtime for collecting the
  // occurrences of each symbol in the input. This information is needed
  // by Hunt's algorithm and can be used to estimate the runtime of both
  // algorithms. If the worst case runtime for Myers' algorithm outperforms
  // the best case runtime of Hunt's algorithm (including the initialization
  // step), we skip the initialization completely.
  float init_factor() const { return init_factor_; }
  // This factor is used to estimate the runtime for setting up data structures
  // estimating the runtime of Myers' and Hunt's algorithm. It is separated from
  // the init_factor, since in one case we have to iterate over the left input
  // sequence and in the other case over the right.
  float estimate_factor() const { return estimate_factor_; }

  // The meaning of the different parameters is explained above.
  void set_max_memory(int max_memory) { max_memory_ = max_memory; }
  void set_max_keys(int max_keys) { max_keys_ = max_keys; }
  void set_lcs_bound_ratio(float ratio) { lcs_bound_ratio_ = ratio; }
  void set_hunt_factor(float factor) { hunt_factor_ = factor; }
  void set_myers_factor(float factor) { myers_factor_ = factor; }
  void set_init_factor(float factor) { init_factor_ = factor; }
  void set_estimate_factor(float factor) { estimate_factor_ = factor; }

 private:
  float hunt_factor_;
  float myers_factor_;
  float init_factor_;
  float estimate_factor_;
  float lcs_bound_ratio_;
  int max_memory_;
  int max_keys_;
};

// Representation of a chunk which occurs in two sequences.
struct Chunk {
  Chunk(int l, int r, int len)
      : left(l), right(r), length(len) {
  }

  int left;    // First common item in the left sequence.
  int right;   // First common item in the right sequence.
  int length;  // Number of identical items in both sequences.
};

class Lcs {
 public:
  void set_options(const LcsOptions& options);
  LcsOptions* mutable_options();

  // Computes the longest common subsequence between the two integer sequences.
  // The return value represents the length of the longest common subsequence.
  // The actual common sequence is represented by the chunks vector where each
  // chunk represents contiguous matches ordered by ascending positions. If one
  // is only interested in the length of the longest common subsequence, the
  // last argument should be set to NULL. This will reduce the overall runtime,
  // since the overhead of tracking and reconstructing the actual sequence can
  // be avoided.
  //
  // If the LCS computation has to be aborted due to set memory or runtime
  // limits, it will return a negative value and the chunks vector will be
  // empty. The meaning of the negative value is described in the LcsErrorCodes
  // enum.
  int Run(const std::vector<int>& left,
          const std::vector<int>& right,
          std::vector<Chunk>* chunks);

  // Same as above.
  int Run(const int* left, int left_size, const int* right, int right_size,
          std::vector<Chunk>* chunks);

  // Same as above but with strings as input.
  // Note that the algorithm is not UTF-8 aware and treats each byte as a
  // separate unit.
  int Run(absl::string_view left, absl::string_view right,
          std::vector<Chunk>* chunks);

  // Returns the number of different integers generated by the mapping.
  // Only non-negative integers smaller than this value occur in the mapping.
  // Two entries of the mapped containers (left_int and right_int) are equal
  // if the corresponding entries of the original containers (left and right)
  // are equal. Entries which occur only on one side are mapped to the same
  // integer such that the minimal number of distinct integers is generated.
  template <class ValueType, class Container>
  static int MapToInteger(const Container& left,
                          const Container& right,
                          std::vector<int>* left_int,
                          std::vector<int>* right_int);

 private:
  LcsOptions options_;
};

template <class ValueType, class Container>
int Lcs::MapToInteger(const Container& left,
                      const Container& right,
                      std::vector<int>* left_int,
                      std::vector<int>* right_int) {
  absl::flat_hash_map<ValueType, int> hash;

  const int left_size = left.size();
  const int right_size = right.size();
  left_int->clear();
  right_int->clear();
  left_int->reserve(left_size);
  right_int->reserve(right_size);

  // Create integer values for the right side.
  bool has_sentinel = false;
  for (const auto& right_entry : right) {
    // flat_hash_map requires that the constructed key in the map is equal to
    // the insertion key. A default constructed ProcessedEntry is equal to
    // nothing and can be trivially left out of the map.
    if (!(right_entry == right_entry)) {
      has_sentinel = true;
      right_int->push_back(-1);
    } else {
      const int mapped = file_based_test_driver_base::LookupOrInsert(&hash, right_entry, hash.size());
      right_int->push_back(mapped);
    }
  }

  // Map left side to the same integers as for the right side.
  // Values not occurring on the right side are mapped to a single integer.
  int num_right_keys = hash.size();
  std::vector<int> used_by_left(num_right_keys + 1, 0);
  for (const auto& left_entry : left) {
    const int mapped = file_based_test_driver_base::FindWithDefault(hash, left_entry, num_right_keys);
    left_int->push_back(mapped);
    // Mark that mapped occurs on the left.
    used_by_left[mapped] = 1;
  }

  // Compact the range of integers by ignoring integers which occur only on the
  // right side. This is purely an optimization which can reduce the processing
  // time and memory consumption of subsequent steps, and is not required for
  // algorithmic correctness.
  // First, assign each integer a new value. Values not occurring on the right
  // are mapped to the integer value not_occurring.
  int num_new_keys = 0;
  int not_occurring = has_sentinel ? num_new_keys++ : -1;
  for (int k = 0; k <= num_right_keys; ++k) {
    if (used_by_left[k]) {
      used_by_left[k] = num_new_keys++;
    } else if (k < num_right_keys) {
      if (not_occurring == -1)
        not_occurring = num_new_keys++;
      used_by_left[k] = not_occurring;
    }
  }
  // Second, update the already integer values.
  for (int i = 0; i < left_size; ++i)
    (*left_int)[i] = used_by_left[(*left_int)[i]];
  for (int j = 0; j < right_size; ++j) {
    int& ri = (*right_int)[j];
    if (ri == -1) {
      ri = not_occurring;
    } else {
      ri = used_by_left[ri];
    }
  }
  return num_new_keys;
}

}  // namespace file_based_test_driver_base

#endif  // THIRD_PARTY_FILE_BASED_TEST_DRIVER_BASE_LCS_H__

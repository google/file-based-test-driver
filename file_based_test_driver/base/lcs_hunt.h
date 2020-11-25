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

// Implementation of Hunt's algorithm for computation of Longest Common
// Subsequence as described in "An Algorithm for Differential File Comparison"
// by Hunt and McIlroy (see http://www.cs.dartmouth.edu/~doug/diff.ps).
// It expects the right sequence being represented as a KeyOccurrences data
// structure in order to efficiently iterate through matches.
#ifndef THIRD_PARTY_FILE_BASED_TEST_DRIVER_BASE_LCS_HUNT_H_
#define THIRD_PARTY_FILE_BASED_TEST_DRIVER_BASE_LCS_HUNT_H_

#include <algorithm>
#include <iterator>
#include <vector>

#include "file_based_test_driver/base/logging.h"
#include "file_based_test_driver/base/lcs.h"
#include "file_based_test_driver/base/lcs_util.h"

namespace file_based_test_driver_base {
namespace internal {

template <class T> class LcsHybrid;

// KeyOccurrences enables O(1) access time to occurrences of keys. To keep
// the memory consumption low (= sizeof(int) * (max_key + items_size)), the
// internal data is stored in two vectors which makes the memory consumption
// dependent on the maximum key. By a preceding compaction of the key space,
// the maximum key can always be reduced to at least items_size: (value + first)
//
// A straight forward representation would use a hash container from `int` to
// `vector<int>`. However, the memory consumption can be more than twice as big
// in the worst case for the memory efficient `sparse_hash_map` and more than 4
// times as big for the fast `absl::flat_hash_map`. With the proposed solution,
// we have a fast and compact solution, although a preceding compaction step
// might be needed depending on the input.
class KeyOccurrences {
 public:
  // Stores occurrences for each key in a compact yet efficient representation.
  // The maximum occurring key + 1 must be passed in as argument keys.
  template <class Item>
  void Init(const Item* items, int items_size, int keys);

  // Given a key, returns the positions where this key occurs in the item
  // sequence with which the KeyOccurrences instance has been initialized.
  void occurrences(int key, const int** begin, const int** end) const {
    // Is the key within the key-range?
    if (key + 1 < static_cast<int>(first_match_.size())) {
      *begin = occurrences_.data() + first_match_[key];
      *end   = occurrences_.data() + first_match_[key + 1];
    } else {
      *begin = nullptr;
      *end = nullptr;
    }
  }

  // Returns the total number of occurrences over all keys.
  int size() const {
    return occurrences_.size();
  }

  void Clear() {
    first_match_.clear();
    occurrences_.clear();
  }

 private:
  friend class LcsStats;
  std::vector<int> first_match_;
  std::vector<int> occurrences_;
};

// Store a match and a pointer to the preceding match.
struct BackPointer {
  int x, y;
  int predecessor;
};

// Compare two backpointers by the index of their right match.
// The waves vectors below are sorted by these indices.
struct LessY {
  explicit LessY(const std::vector<BackPointer>* pointers)
      : pointers_(pointers) {}

  bool operator()(int i, int j) const {
    return (*pointers_)[i].y < (*pointers_)[j].y;
  }

  const std::vector<BackPointer>* pointers_;
};

// Implementation of LCS algorithm as described in "An Algorithm for
// Differential File Comparison" by Hunt and McIlroy.
// (see http://www.cs.dartmouth.edu/~doug/diff.ps)
template <class ItemT>
class LcsHunt {
 public:
  typedef ItemT Item;

  // Computes LCS with Hunts algorithm.
  // Returns the length of the longest common subsequence.
  // If chunks is set to nullptr, the more efficient version without
  // backpointers is chosen. Otherwise, the vector will be filled with the
  // matching pieces.
  int Run(const Item* left, int left_size, int left_offset,
          const KeyOccurrences& right_occ, int right_offset,
          std::vector<Chunk>* chunks);

  int split_x() const { return split_x_; }
  int split_y() const { return split_y_; }

 private:
  // Computes LCS while tracking back pointers for reconstruction of matches.
  // The matches are written to the chunks vector.
  // Reserves quadratic amount of memory for storing back pointers.
  // Returns the length of the longest common subsequence.
  int RunAndReport(const Item* left, const int left_size, int left_offset,
                   const KeyOccurrences& right_occ, int right_offset,
                   std::vector<Chunk>* chunks);

  // Computes LCS without tracking back pointers with linear memory overhead.
  // It uses a bidirectional search to find a split point which can be used by
  // a recursive algorithm to reconstruct the actual matches in a recursive
  // manner while requiring only linear working memory.
  int RunSplit(const Item* left, const int left_size,
               const KeyOccurrences& right_occ);

  // Split point position.
  int split_x_;
  int split_y_;
};

template <class Item>
void KeyOccurrences::Init(const Item* items, int items_size, int keys) {
  // Compute number of occurrences per key.
  first_match_.clear();
  first_match_.resize(keys + 1);
  for (int i = 0; i < items_size; i++)
    first_match_[items[i]]++;
  // Accumulate number of occurrences per key. This gives the index where the
  // occurrences of a specific key can be found in the array occurrences_.
  int acc = 0;
  for (int i = 0; i <= keys; i++) {
    int tmp = acc;
    acc += first_match_[i];
    first_match_[i] = tmp;
  }
  // Store the occurrences sorted by keys.
  std::vector<int> insertion_point = first_match_;
  occurrences_.resize(items_size);
  // We need the matches in reverse order.
  for (int i = items_size - 1; i >= 0; --i)
    occurrences_[insertion_point[items[i]]++] = i;
}


template <class ItemT>
int LcsHunt<ItemT>::Run(const Item* left, int left_size, int left_offset,
                        const KeyOccurrences& right_occ, int right_offset,
                        std::vector<Chunk>* chunks) {
  if (chunks)
    return RunAndReport(left, left_size, left_offset,
                        right_occ, right_offset, chunks);
  else
    return RunSplit(left, left_size, right_occ);
}

template <class ItemT>
int LcsHunt<ItemT>::RunAndReport(
    const Item* left, const int left_size, int left_offset,
    const KeyOccurrences& right_occ, int right_offset,
    std::vector<Chunk>* chunks) {
  const int right_size = right_occ.size();

  std::vector<BackPointer> back_pointers;
  back_pointers.reserve(left_size + right_size + 2);
  back_pointers.resize(3);
  // Backpointer before any match.
  back_pointers[0].x = -1;
  back_pointers[0].y = -1;
  // Backpointer after any match.
  back_pointers[1].x = left_size + right_size + 1;
  back_pointers[1].y = left_size + right_size + 1;
  // The last backpointer is always used as a temporary one which simplifies
  // the search with lower_bound.

  // Initialize the wave front with 0, 1, 1, 1, ... i.e. one backpointer
  // before any match and the rest with backpointers after any match.
  std::vector<int> waves_fwd(right_size + 1, 1);
  waves_fwd[0] = 0;

  LessY cmp(&back_pointers);
  for (int x = 0; x < left_size; ++x) {
    const int *begin, *end;
    right_occ.occurrences(left[x], &begin, &end);
    for (; begin < end; ++begin) {
      // Find potential insertion point for *begin in the waves_fwd vector.
      // Use the temporary last entry of the back_pointers for this whose
      // other members will be set, when it will be inserted into the
      // waves_fwd vector.
      BackPointer* tmp = &back_pointers.back();
      tmp->y = *begin;
      // Find insertion point for temporary entry in the waves_fwd vector.
      int l = std::lower_bound(waves_fwd.begin(), waves_fwd.end(),
                               back_pointers.size() - 1, cmp) -
              waves_fwd.begin();
      BackPointer& other(back_pointers[waves_fwd[l]]);
      // Should the new entry be inserted into the waves_fwd vector?
      if (other.y == tmp->y)
        continue;
      tmp->x = x;  // Save the x coordinate of the match.
      tmp->predecessor = waves_fwd[l - 1];  // Remember the previous match.
      // The new entry represents a chain of l matching characters. Since
      // it ends with a lower y coordinate than the previous entry
      // waves_fwd[l], the new entry overwrites the previous one.
      waves_fwd[l] = back_pointers.size() - 1;
      back_pointers.push_back(*tmp);  // Create a new temporary entry.
    }
  }

  // Report chunks.
  int l;  // l represents the number of matches for corresponding bp chain.
  // Thus, the largest valid entry in waves_fwd corresponds to the lcs.
  for (l = waves_fwd.size() - 1; l >= 0 && waves_fwd[l] == 1; --l);
  // Report each match and follow its backpointer to the next match.
  int bp = waves_fwd[l];
  int first_chunk = chunks->size();
  while (bp) {
    AppendReverseChunk(back_pointers[bp].x + left_offset,
                       back_pointers[bp].y + right_offset, 1, chunks);
    bp = back_pointers[bp].predecessor;
  }
  ReorderReverseChunks(first_chunk, chunks);
  return l;
}

template <class ItemT>
int LcsHunt<ItemT>::RunSplit(const Item* left, const int left_size,
                             const KeyOccurrences& right_occ) {
  const int right_size = right_occ.size();
  const int *begin, *end;

  std::vector<int> waves_fwd_y(right_size + 1, right_size + 1);
  waves_fwd_y[0] = -1;
  std::vector<int> waves_bwd_y(right_size + 1, -1);
  waves_bwd_y[right_size] = right_size + 1;
  std::vector<int> waves_fwd_x(right_size + 1, -1);
  std::vector<int> waves_bwd_x(right_size + 1, -1);

  // Compute the LCS for the first half of left without remember backpointers.
  split_x_ = left_size/2;
  for (int x = 0; x <= split_x_; ++x) {
    right_occ.occurrences(left[x], &begin, &end);
    for (; begin < end; ++begin) {
      int y = *begin;
      int l = std::lower_bound(waves_fwd_y.begin(), waves_fwd_y.end(), y) -
              waves_fwd_y.begin();
      waves_fwd_x[l] = x;
      waves_fwd_y[l] = y;
    }
  }
  // Compute the LCS for the second half of left.
  for (int x = left_size - 1; x > split_x_; --x) {
    right_occ.occurrences(left[x], &begin, &end);
    while (end > begin) {
      int y = *--end;
      int l = std::upper_bound(waves_bwd_y.begin(), waves_bwd_y.end(), y) -
              waves_bwd_y.begin() - 1;
      waves_bwd_x[l] = x;
      waves_bwd_y[l] = y;
    }
  }
  // Merge both results by looking for the optimal splitting point in right.
  int lcs = 0;  // In the worst case, we have no matches.
  split_y_ = 0;  // Choose a valid default split point.
  // l represents the number of matches in the first half.
  // right_size - k represents the number of matches in the second half.
  for (int l = 0, k = 0; l <= right_size && waves_fwd_y[l] < right_size; l++) {
    // Matches consumed by the first half must not be consumed by second half.
    // Adjust k until this condition is satisfied.
    while (waves_bwd_y[k] <= waves_fwd_y[l])
      k++;
    // Has the new split point more overall matches?
    if (lcs < l + right_size - k) {
      lcs = l + right_size - k;
      // Entry 0 does not point to an actual match.
      split_x_ = l ? waves_fwd_x[l] : waves_bwd_x[k];
      split_y_ = l ? waves_fwd_y[l] : waves_bwd_y[k];
    }
  }
  return lcs;
}

}  // namespace internal
}  // namespace file_based_test_driver_base

#endif  // THIRD_PARTY_FILE_BASED_TEST_DRIVER_BASE_LCS_HUNT_H_

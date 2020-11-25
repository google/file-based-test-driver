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

#ifndef THIRD_PARTY_FILE_BASED_TEST_DRIVER_BASE_LCS_MYERS_H_
#define THIRD_PARTY_FILE_BASED_TEST_DRIVER_BASE_LCS_MYERS_H_

#include <algorithm>
#include <deque>
#include <vector>

#include "file_based_test_driver/base/lcs.h"
#include "file_based_test_driver/base/lcs_util.h"

namespace file_based_test_driver_base {
namespace internal {

// Implementation of Myers' algorithm (http://www.xmailserver.org/diff2.pdf).
// Its complexity is O((left_size + right_size) * D), where D is the difference
// between the two sequences. The computation of a split point together with
// the actual difference requires only
//   O(D) <= O(max_diff) <= O(left_size + right_size).
// If the actual chunks should be computed in the same pass, the memory
// consumption increases to O(D * D). Embedding Myers' algorithm into a
// recursive scheme, the chunks can actually be computed with linear memory
// overhead while keeping the overall runtime mentioned above.
template <class ItemT>
class LcsMyers {
 public:
  typedef ItemT Item;

  // Run Myers' algorithm on the two sequences. If chunks is not nullptr, the
  // matching chunks will be returned. Otherwise, only the difference and
  // a split point will be computed. This will save memory (only linear
  // memory consumption instead of quadratic) and reduce runtime, since
  // saving of backpointers is not needed.
  // Returns the length of the longest common subsequence.
  // Returns kLcsMaxDiffExceeded, if the longest common subsequence has more
  // mismatches than specified by maximum difference.
  int Run(const Item* left, int left_size, int left_offset,
          const Item* right, int right_size, int right_offset,
          std::vector<Chunk>* chunks);

  int split_x() const { return split_x_; }
  int split_y() const { return split_y_; }

 private:
  // Computes the y coordinate for a given x coordinate and the forward diagonal
  // specified by k_fwd and D.
  int ComputeYForward(int x, int k_fwd, int D) {
    return x - 2 * k_fwd + D;
  }

  // Computes the y coordinate for a given x coordinate and the reverse diagonal
  // specified by k_fwd and D.
  int ComputeYReverse(int x, int k_rev, int D, int left_size, int right_size) {
    return right_size - left_size - D + x + 2 * k_rev;
  }

  // Transforms a forward diagonal into the corresponding reverse diagonal
  // (assuming that the reverse search has one mismatch less).
  int ComputeKReverse(int k_fwd, int D, int left_size, int right_size) {
    return D - k_fwd - (right_size - left_size + 1) / 2;
  }

  // Transforms a reverse diagonal into the corresponding forward diagonal
  // (assuming that both searches have the same number of mismatches).
  int ComputeKForward(int k_rev, int D, int left_size, int right_size) {
    return (left_size - right_size) / 2 - k_rev + D;
  }

  // Store a split point. The Report function follows backpointers from the
  // split point in forward and reverse direction.
  void SaveSplitPoint(int k_fwd, int k_rev, int x, int diff);

  // Do the actual LCS search.
  template <bool kOddDelta, bool kSaveBackpointers>
  void RunInternal(const Item* left, int left_size,
                   const Item* right, int right_size);

  // Report the chunks by following the stored backpointers.
  void Report(int left_size, int left_offset, int right_size, int right_offset,
              std::vector<Chunk>* chunks);

  // Keep track of the backpointers.
  std::deque<int> preceding_x_fwd_;
  std::deque<int> preceding_x_rev_;
  // Information about the split point.
  int split_x_;
  int split_y_;
  int split_k_fwd_;
  int split_k_rev_;
  // Difference between the two sequences.
  int diff_;
};

template <class ItemT>
int LcsMyers<ItemT>::Run(const Item* left, int left_size, int left_offset,
                         const Item* right, int right_size, int right_offset,
                         std::vector<Chunk>* chunks) {
  bool odd_delta = (left_size - right_size) & 1;
  if (odd_delta && chunks != nullptr)
    RunInternal<true, true>(left, left_size, right, right_size);
  else if (odd_delta)
    RunInternal<true, false>(left, left_size, right, right_size);
  else if (chunks != nullptr)
    RunInternal<false, true>(left, left_size, right, right_size);
  else
    RunInternal<false, false>(left, left_size, right, right_size);
  if (chunks != nullptr && diff_ >= 0)
    Report(left_size, left_offset, right_size, right_offset, chunks);
  return (left_size + right_size - diff_) / 2;
}

template <class ItemT>
template <bool kOddDelta, bool kSaveBackpointers>
void LcsMyers<ItemT>::RunInternal(const Item* left, int left_size,
                                  const Item* right, int right_size) {
  const int kMax = (left_size + right_size + 1) / 2;
  std::vector<int> best_x_fwd(kMax + 1);
  std::vector<int> best_x_rev(kMax + 1);
  if (kSaveBackpointers) {
    preceding_x_fwd_.clear();
    preceding_x_rev_.clear();
  }
  // Do two shortest-path searches in parallel. The one starting from (0,0) is
  // called forward search, the other starting from (left_size, right_size) is
  // called reverse search. Stop until either both search intersect or
  // the maximum number of mismatches has been reached.
  for (int D = 0; D <= kMax; D++) {
    // Append one mismatch to the forward shortest-paths of previous loop and
    // follow as many matches as possible.
    // prev and best_x_fwd[D] should never be chosen for backreferences
    // (except for D = 0) which is guaranteed by the following initialization.
    int prev = -1;
    best_x_fwd[D] = 0;
    for (int k_fwd = 0; k_fwd <= D; k_fwd++) {
      int next = best_x_fwd[k_fwd];
      if (kSaveBackpointers)
        // Remember the previous x positon and whether the mismatch was in the
        // left or right sequence.
        preceding_x_fwd_.push_back((prev < next) ? next * 2 : prev * 2 + 1);
      int x = prev < next ? next : prev;
      int y = ComputeYForward(x, k_fwd, D);
      prev = next + 1;
      // Consume as many matches as possible.
      while (x < left_size && y < right_size && left[x] == right[y]) {
        ++x;
        ++y;
      }
      best_x_fwd[k_fwd] = x;
      int k_rev = ComputeKReverse(k_fwd, D, left_size, right_size);
      // Check whether the forward search overlaps with the reverse search.
      if (kOddDelta && k_rev >= 0 && k_rev < D && best_x_rev[k_rev] <= x) {
        SaveSplitPoint(k_fwd, k_rev, x, D * 2 - 1);
        return;
      }
    }
    best_x_rev[D] = left_size;
    prev = left_size;
    // Append one mismatch to the reverse shortest-paths of previous loop and
    // follow as many matches as possible.
    // prev and best_x_fwd[D] should never be chosen for backreferences
    // (except for D = 0) which is guaranteed by the following initialization.
    for (int k_rev = 0; k_rev <= D; k_rev++) {
      int next = best_x_rev[k_rev];
      if (kSaveBackpointers)
        preceding_x_rev_.push_back((prev >= next) ? next * 2 : prev * 2 + 1);
      int x = (prev >= next) ? next : prev;
      int y = ComputeYReverse(x, k_rev, D, left_size, right_size);
      prev = next - 1;
      while (x > 0 && y > 0 && left[x - 1] == right[y - 1]) {
        --x;
        --y;
      }
      best_x_rev[k_rev] = x;
      int k_fwd = ComputeKForward(k_rev, D, left_size, right_size);
      if (!kOddDelta && k_fwd >= 0 && k_fwd <= D && x <= best_x_fwd[k_fwd]) {
        SaveSplitPoint(k_fwd, k_rev, best_x_fwd[k_fwd], D * 2);
        return;
      }
    }
  }
  diff_ = kLcsMaxDiffExceeded;  // kMax limit reached
}

template <class ItemT>
void LcsMyers<ItemT>::SaveSplitPoint(int k_fwd, int k_rev, int x, int diff) {
  diff_ = diff;
  split_k_fwd_ = k_fwd;
  split_k_rev_ = k_rev;
  split_x_ = x;
  split_y_ = ComputeYForward(x, k_fwd, (diff + 1) / 2);
}

template <class ItemT>
void LcsMyers<ItemT>::Report(int left_size, int left_offset, int right_size,
                             int right_offset, std::vector<Chunk>* chunks) {
  // Report the chunks before the split point by following the backpointers
  // of the forward search. The split point has chosen in a way that all
  // chunks of the forward search can be reported.
  int D = (diff_ + 1) / 2;
  int k_fwd = split_k_fwd_;
  int x = split_x_;
  int first_chunk = chunks->size();
  for (; D >= 0; --D) {
    int bp = preceding_x_fwd_[((D + 1) * (D) / 2 + k_fwd)];
    int len = x - (bp / 2);  // (bp / 2) decodes the preceding x position.
    x -= len;
    int y = ComputeYForward(x, k_fwd, D);
    AppendReverseChunk(x + left_offset, y + right_offset, len, chunks);
    if (bp & 1) {  // Is the mismatch on the left or right sequence?
      x--;
      k_fwd--;
    }
  }
  ReorderReverseChunks(first_chunk, chunks);

  // Report the chunks after the split point by following the backpointers
  // of the reverse search. Matches before the split point have to be
  // excluded.
  D = diff_ / 2;
  int k_rev = split_k_rev_;
  // Start point for next chunk. (Can result in a negative len during the
  // first iteration.)
  x = split_x_;
  for (; D >= 0; --D) {
    int bp = preceding_x_rev_[(D * (D + 1) / 2 + k_rev)];
    int len = (bp / 2) - x;
    int y = ComputeYReverse(x, k_rev, D, left_size, right_size);
    // Compute portion of chunk which lies before the split point.
    // In case of a negative len, this will return len.
    int skip = std::min(len, std::max(0, std::max(split_x_ - x, split_y_ - y)));
    // Only report the part of the chunk which lies after the split point.
    AppendChunk(x + skip + left_offset, y + skip + right_offset, len - skip,
                chunks);
    x += len;
    if (bp & 1) {  // Is the mismatch on the left or right sequence?
      x++;
      k_rev--;
    }
  }
}

}  // namespace internal
}  // namespace file_based_test_driver_base

#endif  // THIRD_PARTY_FILE_BASED_TEST_DRIVER_BASE_LCS_MYERS_H_

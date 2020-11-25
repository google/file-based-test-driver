//
// Copyright 2007 Google LLC
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

#ifndef THIRD_PARTY_FILE_BASED_TEST_DRIVER_BASE_LCS_INL_H__
#define THIRD_PARTY_FILE_BASED_TEST_DRIVER_BASE_LCS_INL_H__

#include <algorithm>
#include <functional>
#include <hash_map>
#include <iterator>
#include <list>
#include <string>
#include <utility>
#include <vector>

#include "file_based_test_driver/base/logging.h"
#include "absl/container/node_hash_map.h"

// This module implements the longest common subsequence problem for
// a generic container type.  The algorithm implemented is as described by
// Hunt and McIlroy in "An Algorithm for Differential File Comparison"
// (see http://www.cs.dartmouth.edu/~doug/diff.ps).
//
// Results are written to a MatchList, which lists pairs of indices
// and lengths of matches.  MatchList is a list<pair<pair<int, int>, int> >
// Each element <<i, j>, n> is an assertion that first[i] == second[j],
// first[i+1] == second[j+1], ..., and first[i+n-1] == second[j+n-1].
// The list will be monotonically increasing in i and j.
//
// Since it is possible to call this on almost any container, including
// linked lists, you must provide the size of each sequence when calling lcs().
// This prevents the algorithm from needing to execute size(), which can be
// O(n) for some data structures.

namespace file_based_test_driver_base {
namespace internal {
// Back pointers represent a single match between the two sequences, plus
// a pointer to the previous match.  At the end of the algorithm, we find the
// "best" BackPointer, and follow the pointers backwards until we have
// generated a complete list of matching indices.
// Refcounting helps keep storage as small as possible at all times.
class BackPointer {
 public:
  BackPointer(int index_left, int index_right, BackPointer* ptr) :
      index_left_(index_left),
      index_right_(index_right),
      ptr_(ptr),
      refcount_(1) {}
  BackPointer(const BackPointer&) = delete;
  BackPointer& operator=(const BackPointer&) = delete;
  BackPointer() = delete;

  BackPointer* next() { return ptr_; }
  // We only ever set the right index, for making sorting keys
  void set_index(int index) { index_right_ = index; }
  int index_left() const { return index_left_; }
  int index_right() const { return index_right_; }

  void Ref() { ++refcount_; }
  void Unref() {
    --refcount_;
    if (refcount_ == 0) delete this;
  }
 private:
  ~BackPointer() {
    // We may have a potentially-large chain of references where each
    // BackPointer holds the sole reference to another BackPointer.
    // If we simply call Unref() on these, this can result in stack overflow
    // when the chain is large.  To prevent this problem, we delete such
    // chains iteratively.
    while (ptr_ && ptr_->refcount_ == 1) {
      BackPointer* next = ptr_->ptr_;
      // Null out its child; we're taking responsibility for that child now.
      ptr_->ptr_ = nullptr;
      delete ptr_;
      ptr_ = next;
    }
    if (ptr_) ptr_->Unref();
  }
  // index_left_ and index_right_ are indices into the two sequences
  // (left being the first file, right being the second, "changed" file).  The
  // existence of a back pointer with this combination of indices asserts
  // that left[index_left_] == right[index_right_].
  int index_left_;
  int index_right_;
  BackPointer* ptr_;  // the previous match
  int refcount_;
};

struct cmp_back_pointers {
  bool operator()(const BackPointer* const& left,
                  const BackPointer* const& right) const {
    // We compare based on the right index
    return left->index_right() < right->index_right();
  }
};
}  // namespace internal

typedef std::list<std::pair<std::pair<int, int>, int> > MatchList;

// Longest common subsequence function, templated by container type.
// You can use any container that allows forward and reverse iterators,
// and the objects it contains must be hashable.
template <class Container,
          class Hash = std::hash<typename Container::value_type>,
          class Pred = std::equal_to<typename Container::value_type>>
    void LCS(const Container& first,
             const Container& second,
             int first_size,
             int second_size,
             MatchList* out) {
  using file_based_test_driver_base::internal::BackPointer;
  using file_based_test_driver_base::internal::cmp_back_pointers;
  // We need to distill a map where each member of second maps onto a
  // list of indices (in decreasing order) where that member is found
  // in second.
  absl::node_hash_map<typename Container::value_type, std::vector<int>*, Hash,
                      Pred>
      line_map;

  // To get the lines in reverse order, we iterate over second backwards
  int second_index = second_size - 1;
  for (typename Container::const_reverse_iterator it = second.rbegin();
       it != second.rend();
       ++it, --second_index) {
    std::vector<int>*& bucket = line_map[*it];
    if (bucket == nullptr) bucket = new std::vector<int>;
    bucket->push_back(second_index);
  }

  int size = std::max(first_size, second_size) + 1;
  // Our array of BackPointers.  array[n] will contain the best known
  // BackPointer for constructing a subsequence of length n.  We initialize
  // array with fence values that point beyond the end of the two sequences.
  // After constructing array, we search for the last non-fence value and
  // follow those BackPointers.
  std::vector<BackPointer*> array(size);
  // Create a toy negative entry that will act as the final back pointer
  // for every sequence.
  array[0] = new BackPointer(-2, -2, nullptr);
  for (int i = 1; i < array.size(); ++i) {
    // "size" is greater than the max possible value.
    array[i] = new BackPointer(size, size, nullptr);
  }

  cmp_back_pointers compare;

  // tmp BackPointer used in lower_bounds.
  BackPointer* tmp = new BackPointer(0, 0, nullptr);
  int first_index = 0;
  for (typename Container::const_iterator it = first.begin();
       it != first.end();
       ++it, ++first_index) {
    // The real work: for every match between first and second, find the
    // lowest slot in the array where we could stick that index,
    // then connect all of the back pointers.
    std::vector<int>* v = line_map[*it];
    if (!v) {
      // The second file doesn't contain a line that looks like this.
      // No new matches.
      continue;
    }
    for (int j = 0; j < v->size(); ++j) {
      tmp->set_index(v->at(j));
      std::vector<BackPointer*>::iterator back_it =
          std::lower_bound(array.begin(), array.end(), tmp, compare);
      // Only change the BackPointers if we have something strictly less
      // than what currently exists at this location.  If we have the same
      // value, make no changes.
      if ((*back_it)->index_right() == v->at(j)) continue;
      BackPointer* prev = *(back_it - 1);
      prev->Ref();
      (*back_it)->Unref();
      *back_it = new BackPointer(first_index, v->at(j), prev);
    }
  }
  tmp->set_index(size);
  std::vector<BackPointer*>::iterator end =
      std::lower_bound(array.begin(), array.end(), tmp, compare);

  out->clear();
  // Following the BackPointers will give us a sequence of individual matches.
  // We need to "compress" these when we have sets of consecutive matches.
  int last_match_left = -2;   // The last index_left we processed
  int last_match_right = -2;  // likewise for index_right
  int match_length = 0;       // Length (so far) of current match region
  // *end is the first fence that still remains, so *(end-1) is the BackPointer
  // we must follow to reconstruct the LCS.
  for (BackPointer* ptr = *(end - 1); ptr != nullptr; ptr = ptr->next()) {
    if (ptr->index_left() + 1 == last_match_left &&
        ptr->index_right() + 1 == last_match_right) {
      // This continues a consecutive region; increment match_length
      ++match_length;
    } else {
      // We're starting to match a new region now.  Emit the previous match.
      if (last_match_left >= 0)
        out->push_front(std::make_pair(
            std::make_pair(last_match_left, last_match_right), match_length));
      // Reset match_length
      match_length = 1;
    }
    last_match_left = ptr->index_left();
    last_match_right = ptr->index_right();
  }
  // Since all BackPointer sequences end at our toy (-2, -2) pair, we've
  // already emitted the last matched region.
  FILE_BASED_TEST_DRIVER_CHECK_EQ(last_match_left, -2);
  FILE_BASED_TEST_DRIVER_CHECK_EQ(last_match_right, -2);
  FILE_BASED_TEST_DRIVER_CHECK_EQ(match_length, 1);

  // Cleanup - Unref the last references we hold.
  for (int i = 0; i < array.size(); ++i) {
      array[i]->Unref();
  }
  for (auto entry : line_map) {
    delete entry.second;
  }
  tmp->Unref();
}
}  // namespace file_based_test_driver_base

#endif  // THIRD_PARTY_FILE_BASED_TEST_DRIVER_BASE_LCS_INL_H__

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

#ifndef THIRD_PARTY_FILE_BASED_TEST_DRIVER_BASE_REDIFF_H__
#define THIRD_PARTY_FILE_BASED_TEST_DRIVER_BASE_REDIFF_H__

#include <stddef.h>
#include <string.h>

#include <list>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/macros.h"
#include "absl/strings/string_view.h"
#include "base/diffchunk.h"
#include "base/lcs.h"

namespace file_based_test_driver_base {
struct DiffChunk;

// A ProcessedEntry is a struct that wraps a single line (or logical chunk)
// of a file that is being diffed.  The ProcessedEntry keeps track of its line
// number as well as some metadata associated with the line.
//
// Every ProcessedEntry has a "score" associated with it; this is intended to
// be a measure of the amount of "information" encoded in the string.  A simple
// scoring function might be the length of the string.  The "tolerance" value
// employed by ReDiff are compared with the total score of all ProcessedEntries
// in a region, to determine if that region has enough information to be
// considered a match, or if ReDiff should pretend that the matched entries
// are non-matching.  Modifying the scoring function and tolerance value can
// achieve a more "pleasing" semantic diff--for example, refusing to match a
// line with a lone "}" character makes the diff algorithm more likely to find
// small edits that break up large regions of matched code.
//
// Score can be explicitly assigned with some of the ProcessedEntry
// constructors, offering the greatest degree of control.  Alternatively,
// score can be computed from a "score matrix" -- an array of 256 ints that
// indicate the score value that should be assigned to each character in the
// string.  The value at index 0 is special--it is added to the score of every
// string, even a string of length 0.  We provide a default score matrix that
// is optimized for UI code diffs -- i.e. for pleasing display of code
// written in (mainly) C++, Java, or Python conforming to the Google style
// guide.  You may also provide a custom scoring matrix when constructing
// ProcessedEntries.
struct ProcessedEntry {
  // Null constructor; this returns a null ProcessedEntry that will compare
  // inequal against every other ProcessedEntry, including itself.
  ProcessedEntry() : data(nullptr), length(-1), number(-1), score(0) {}

  // Construct a new ProcessedEntry directly. The caller is responsible for
  // assigning line numbers to the strings. The individual strings do not need
  // to be null-terminated.
  // This entry is scored with the default score matrix.
  ProcessedEntry(const char* d, int len, int line_no);

  // Construct a new ProcessedEntry directly.
  // As above, except that a custom score matrix is used to compute the score.
  ProcessedEntry(const char* d, int len, int line_no, const int* score_matrix);

  // Parse a string, separating it by newlines, and append the resulting
  // ProcessedEntries to out. Return the number of entries generated.
  // Data from input is not copied; input must remain valid for the lifetime
  // of the created ProcessedEntrys.
  // The default score matrix is used to score the generated entries.
  static int ProcessString(absl::string_view input,
                           std::list<ProcessedEntry>* out);
  // Variants of the above functions that allow you to specify a custom
  // score matrix for scoring the entries.
  static int ProcessString(absl::string_view input,
                           std::list<ProcessedEntry>* out,
                           const int* score_matrix);
  static int ProcessVector(const std::vector<const char *>& input,
                           std::list<ProcessedEntry>* out,
                           const int* score_matrix);
  static int ProcessVectorOfStrings(const std::vector<std::string>& input,
                                    std::list<ProcessedEntry>* out,
                                    const int* score_matrix);
  static int ProcessVectorOfStringViews(
      const std::vector<absl::string_view>& input,
      std::list<ProcessedEntry>* out, const int* score_matrix);

  bool operator==(const ProcessedEntry& other) const {
    if (length != other.length) return false;
    // data == nullptr means "never match".
    if (data == nullptr || other.data == nullptr) return false;
    // Python interns strings, which means that equal strings will often
    // have the same char* values when lines were pre-split Python-side.
    // This optimization is a huge win for that case.
    if (data == other.data) return true;
    return !memcmp(data, other.data, length);
  }
  bool operator!=(const ProcessedEntry& other) const {
    return !(*this == other);
  }
  // Return the "score" given this fragment by the sliding function.
  // See scoring function comments in ReDiff::SlideRegion() for details.
  int BoundaryScore() const;
  // Return [almost] the last character in this fragment.  If the fragment
  // ends with '\n' we'll return the next-to-last character instead.
  char LastRealChar() const;

  // data is a pointer into the buffer of a string.  We need
  // "length" to tell us where the buffer ends.
  // This allows us to convert a string to a set of ProcessedEntries without
  // copying any portion of the string's character buffer.
  const char* data;
  int length;
  int number;
  int score;

  // The default score matrix used for scoring ProcessedEntries.
  static const int* const DefaultScoreMatrix();

 private:
  static const int* const GenerateDefaultScoreMatrix();
};

inline const int* const ProcessedEntry::DefaultScoreMatrix() {
  static const int* const m = GenerateDefaultScoreMatrix();
  return m;
}

// A DiffMatch is highly similar to an LCS Chunk, but with a few bits of
// information that the diff algorithm wants.
// A single match represents an assertion that some number of entries
// (lines, usually) are equivalent between the two inputs.
// Note that the fields are non-constant because SlideRegion may alter
// the makeup of a match slightly.
struct DiffMatch {
  // Standard constructor
  DiffMatch(int left_start_arg, int right_start_arg, int length_arg) :
      left_start(left_start_arg),
      right_start(right_start_arg),
      length(length_arg),
      ignore_score(false) {}

  // Special constructor, for when you want to indicate that the match cannot
  // be ignored.
  DiffMatch(int left_start_arg,
            int right_start_arg,
            int length_arg,
            bool ignore_score_arg) :
      left_start(left_start_arg),
      right_start(right_start_arg),
      length(length_arg),
      ignore_score(ignore_score_arg) {}

  // Index in the left input where the match begins
  int left_start;
  // Index in the right input where the match begins
  int right_start;
  // Number of consecutive entries that match.
  // That is, left_input[left_start] == right_input[right_start],
  // left_input[left_start+1] == right_input[right_start+1], ...,
  // left_input[left_start+length-1] == right_input[right_start+length-1]
  int length;
  // Used internally: if true, don't allow this match to be rejected due to
  // the "score" that we computed for it.
  const bool ignore_score;
};

class ReDiff {
 public:
  ReDiff();

  ReDiff(const ReDiff&) = delete;
  ReDiff& operator=(const ReDiff&) = delete;

  enum MatchType { MATCHED, UNMATCHED };

  // Execute a diff of two strings.  The results of the diff are stored
  // internally to this object, and can be obtained via ChunksToString/Vector.
  // The strings are split based on newline characters before being diffed.
  // Data from strings are not copied; strings must remain valid for the
  // lifetime of ReDiff.
  void DiffStrings(absl::string_view left, absl::string_view right);

  // Execute a diff of two vectors of char*s.  It is assumed that the caller
  // has split the input based on lines or some other heuristic, and no
  // further splitting of the entries is attempted.  As above, you may
  // retrieve the results of the diff after this call.
  // Data from vectors are not copied; pointers must remain valid for the
  // lifetime of ReDiff.
  void DiffVectors(const std::vector<const char *>& left,
                   const std::vector<const char *>& right);

  // Execute a diff of two vectors of strings.  It is assumed that the caller
  // has split the input based on lines or some other heuristic, and no
  // further splitting of the entries is attempted.  As above, you may
  // retrieve the results of the diff after this call.
  // Data from vectors are not copied; strings must remain valid for the
  // lifetime of ReDiff.
  void DiffVectorsOfStrings(const std::vector<std::string>& left,
                            const std::vector<std::string>& right);

  // Execute a diff of two vectors of string_view.  It is assumed that the
  // caller has split the input based on lines or some other heuristic, and no
  // further splitting of the entries is attempted.  As above, you may
  // retrieve the results of the diff after this call.
  // Data from vectors are not copied; strings must remain valid for the
  // lifetime of ReDiff.
  void DiffVectorsOfStringViews(const std::vector<absl::string_view>& left,
                                const std::vector<absl::string_view>& right);

  // The more general interface: You can fill in the left_side and right_side
  // lists with a sequence of ProcessedEntries yourself, and then call Diff().
  // These functions append a single line to the left or right sides,
  // respectively.
  void push_left(const ProcessedEntry &entry);
  void push_right(const ProcessedEntry &entry);

  // Perform the diff. Requires that left_list_ and right_list_ are
  // initialized.
  void Diff();

  // Get the result of the diff as a string list of opcodes and line numbers.
  // @p out is cleared before the chunks are written.
  void ChunksToString(std::string* out) const;
  // Write the result of the diff to a vector of DiffChunk objects.
  void ChunksToVector(std::vector<DiffChunk>* v) const;

  // Set tolerance levels for diff passes.  If a region of text is matched,
  // but the region is considered to have less than <tolerance> information,
  // then the region is not listed as a match for the final result.
  // @p tolerance is the tolerance to use the
  // text (which determines text that will be marked as unchanged).
  // Tolerance value is just a heuristic for determining how much information
  // is in a given amount of text.  See the CalculateScore procedure to see
  // how this value is calculated.
  void set_tolerance(int tolerance) { tolerance_ = tolerance; }

  // The rest of the api expects to work with a const int* (or int[256]) raw
  // array, however clif can only provide a std::vector<int>, so we have to
  // make a copy of the vec as a raw array and then use that.
  // Rediff should own this new array.
  void set_score_matrix(const int* score_matrix) {
    score_matrix_ = std::vector<int>();
    for (int i = 0; i < 256; i++) {
      score_matrix_.push_back(score_matrix[i]);
    }
  }

  // Exists for backwards compatibility only, take_ownership is unused.
  void set_score_matrix(const int* score_matrix, bool take_ownership) {
    set_score_matrix(score_matrix);
  }

  void set_score_matrix(const std::vector<int>& score_matrix) {
    score_matrix_ = score_matrix;
  }

  // Set options for the underlying LCS algorithm.
  //
  // If the specified memory limit is not sufficient for running LCS, we
  // nevertheless run LCS, but the most memory efficient version.
  // The reasoning behind this decision is that the memory overhead of running
  // rediff is already linear in the input size and LCS can also be computed
  // with linear memory overhead.
  void set_lcs_options(const file_based_test_driver_base::LcsOptions& options) {
    lcs_options_ = options;
  }

 private:
  typedef std::list<ProcessedEntry>::iterator LPEit;

  // Private functions for performing the diff.  Our general strategy for
  // diffing is the following:
  // - Create lists representing the two sides of the diff.  One entry for
  //   every logical unit in the diff (a character, a line, a word, etc.).
  // - Create a corresponding vector for each size that represents matches.
  //   This has one element for every element of the list.  By default, each
  //   match info lists "UNMATCHED" -- that is, we have not found a match for
  //   that line.
  // - Make a series of calls that updates the match vectors.  Find elements
  //   in the lists that match, and mark corresponding elements in the vectors
  //   as MATCHED.  After elements are marked as such, remove them from
  //   the lists (since an element can match only one other element).
  // - Once all desired matches have been determined, we condense these vectors
  //   into a vector of DiffChunks.  Rather than reporting per-line matches,
  //   we report large regions of match info (i.e. lhs lines 100-200 are equal
  //   to rhs lines 120-220).  Also, we convert UNMATCHED regions to one of
  //   INSERT, DELETED, or CHANGED.
  //
  // DiffIteration handles matching of individual lines, while
  // Chunkify and ConvertChunks deal with turning the results into logical
  // chunks.
  int DiffIteration(int tolerance);
  void Chunkify(const std::vector<std::pair<MatchType, int> >& matches,
                std::vector<DiffChunk>* chunks,
                ChunkType unmatched_type);
  static void ConvertChunks(std::vector<DiffChunk>* left_chunks,
                            std::vector<DiffChunk>* right_chunks,
                            std::vector<DiffChunk>* final_chunks);

  static int index_of(int line_number, const std::vector<DiffChunk>& v);

  // Helper functions for DiffIteration:
  // ProcessLeadingMatches
  // Pick off leading matches from our lists.
  // @param left_begin/right_begin: pointers to iterators set to begin() of
  //   left/right_list_ respectively.
  // @return the number of leading matches we have at the top of the file.
  // The lists passed to this function should have a leading and trailing
  // pad entry.  The leading pad will be skipped over, and is not counted
  // in the return value.
  // When this function exits, left_begin/right_begin will have been advanced
  // until they are pointing to the first non-matching line.  In the event
  // that the two lists are identical, this will be the trailing pad.
  int ProcessLeadingMatches(LPEit* left_begin, LPEit* right_begin);
  // ProcessTrailingMatches
  // Very similar to ProcessLeadingMatches.  Pick off trailing matches.
  // @param left_end/right_end: pointers to iterators set to end() of
  //   left/right_list_ respectively.  These are bi-directional iterators, so
  //   we will decrement them to access the list contents.
  // @param leading_matches -- the return value from ProcessLeadingMatches(),
  //   used to help us ensure that the trailing matches do not overlap with
  //   the leading matches.
  // @return the number of trailing matches, as above.
  // As above, we will skip over the trailing pad (which is assumed to be
  // present).  At exit, the iterators will be set to the first matching
  // element in the trailing match for the files.  If there are no trailing
  // matches, the iterators will be pointing at the trailing pad.
  //
  // If you call both ProcessLeadingMatches and ProcessTrailingMatches,
  // the following postconditions will hold:
  // - All 4 iterators used are dereferenceable.
  // - The ranges [left_begin, left_end) and [right_begin, right_end) are
  //   both valid ranges that together comprise a valid input to LCS() that
  //   will detect all differences between the two files.  These ranges
  //   will never include the pads.
  int ProcessTrailingMatches(LPEit* left_end, LPEit* right_end,
                             int leading_matches);
  // ProcessMatchList
  // Process the result of LCS().  This entails:
  // - Post-processing to handle sliding, etc.
  // - Noting matches in left_matches_ and right_matches_.
  // - Removing matched regions from left_list_ and right_list_.
  // @param lcs_result -- the list of matches that resulted from calling LCS
  //   (plus any extra matches generated for matching header/footer entries).
  // @param header_offset -- the total number of entries (including pad)
  //   that were spliced out of the front of the lists (due to leading
  //   matches) before the lists were provided to LCS.  This is used to help
  //   us determine the offset to apply to indices reported by LCS.
  //   However, we require that *lcs_result->begin() must have already had this
  //   offset applied, if necessary.
  // @param tolerance, match_type -- the same args passed to DiffIteration().
  int ProcessMatchList(std::list<DiffMatch>* lcs_result, int header_offset,
                       int tolerance);
  // SlideRegion
  // It's frequent in diffs of code or text to have multiple possible ways to
  // match a region, all of which are correct and maximal.  However, only a
  // few of these are "semantically correct" from the reader's perspective;
  // i.e., the sort of diff a human would have chosen, given the opportunity.
  // This function attempts to slide a matched region left or right to obtain
  // a more semantically correct diff.
  // See http://neil.fraser.name/writing/diff/ - the section on semantic
  // alignment (section 3.2.2 as of 18-Aug-2008) for more details.
  // @param text_it - iterator pointing to the start of the match region
  //   referenced by @p current_match.  You should pass the iterator for the
  //   side of the diff that contains the "extra" text--left for a DELETE,
  //   right for an INSERT.
  // @param current_match - pointer to an iterator that points to the current
  //   match.
  // @param next_match - pointer to an iterator that points just beyond
  //   @p current_match.  That is, ++current_match == next_match.
  // @param gap_length - length (in ProcessedEntries) between the start of
  //   the current match and the start of the next match, for the side that
  //   is passed to this function.
  //   This will be one of the following:
  //     next_match->first.first - current_match->first.first
  //     next_match->first.second - current_match->first.second
  //   (depending on whether the match is an insert or delete).
  // We require pointers for current_match and next_match because we will
  // modify their underlying data in the event that we execute a slide.
  // This technically doesn't require a pointer to the iterator, but we require
  // one to reinforce the idea in the caller that the iterators are modified
  // by this call.
  static void SlideRegion(const LPEit& text_it,
                          std::list<DiffMatch>::iterator* current_match,
                          std::list<DiffMatch>::iterator* next_match,
                          int gap_length);

  ProcessedEntry null_entry_;
  std::vector<std::pair<MatchType, int> > left_matches_;
  std::vector<std::pair<MatchType, int> > right_matches_;
  std::vector<DiffChunk> chunks_;
  std::list<ProcessedEntry> left_list_;
  std::list<ProcessedEntry> right_list_;
  int left_size_;
  int right_size_;
  int tolerance_;
  // score_matrix used for scoring ProcessedEntries.
  // ProcessedEntry::DefaultScoreMatrix() by default
  std::vector<int> score_matrix_;

  LcsOptions lcs_options_;

  friend class RediffInternalsTest;
};

inline void ReDiff::push_left(const ProcessedEntry &e) {
  left_list_.push_back(e);
  ++left_size_;
}

inline void ReDiff::push_right(const ProcessedEntry &e) {
  right_list_.push_back(e);
  ++right_size_;
}

}  // namespace file_based_test_driver_base

#endif  // THIRD_PARTY_FILE_BASED_TEST_DRIVER_BASE_REDIFF_H__

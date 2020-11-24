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

#include "base/rediff.h"

#include <string.h>

#include <algorithm>
#include <iterator>
#include <list>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "base/logging.h"
#include <cstdint>
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "base/diffchunk.h"
#include "base/lcs.h"
#include "base/lcs_hybrid.h"

namespace file_based_test_driver_base {

// Hash operation declaration for ProcessedEntries
template <typename H>
H AbslHashValue(H h, const ProcessedEntry& v) {
  if (v.data == nullptr) {
    return H::combine(std::move(h), absl::string_view());
  } else {
    return H::combine(std::move(h), absl::string_view(v.data, v.length));
  }
}

// Use the new hybrid LCS implementation, but wrap with code that provides
// the API that diff wants to use.
template <class Container>
static void WrapLCS2(const LcsOptions& options,
                     const Container& left, const Container& right,
                     std::list<DiffMatch>* result) {
  std::vector<int> left_int, right_int;
  int keys = Lcs::MapToInteger<ProcessedEntry>(left, right,
                                               &left_int, &right_int);
  Lcs lcs;
  LcsOptions* mutable_options = lcs.mutable_options();
  *mutable_options = options;
  mutable_options->set_max_keys(keys);

  internal::LcsHybridEstimator estimator(left_int.size(), right_int.size(),
                                         mutable_options);
  int64_t recommended_memory = estimator.GetMemoryRecommendation();
  if (recommended_memory > mutable_options->max_memory())
    mutable_options->set_max_memory(recommended_memory);

  std::vector<Chunk> chunks;
  int res = lcs.Run(left_int, right_int, &chunks);
  FILE_BASED_TEST_DRIVER_LOG_IF(WARNING, res < 0)
      << "LCS returned with error code " << res << ".\n"
      << "Rediff will only consider leading/trailing matches";
  for (size_t i = 0; i < chunks.size(); i++)
    result->push_back(
        DiffMatch(chunks[i].left, chunks[i].right, chunks[i].length));
}

// Generate our default scoring matrix--an int array with 256 entries, each
// containing the score for a corresponding character.
const int* const ProcessedEntry::GenerateDefaultScoreMatrix() {
  int* array = new int[256];

  // default score - 2 points per char
  for (int i = 0; i < 256; ++i) array[i] = 2;
  // alphanumeric and '_' (identifier chars): a fair amount of information
  // per character.  3 points per char.
  for (int i = 'a'; i <= 'z'; ++i) array[i] = 3;
  for (int i = 'A'; i <= 'Z'; ++i) array[i] = 3;
  for (int i = '0'; i <= '9'; ++i) array[i] = 3;
  array['_'] = 3;
  // Whitespace -- no effect on score.
  array[' '] = array['\t'] = array['\n'] = array['\r'] = 0;
  // Also no points for parens, commas, or characters that commonly start
  // comments.  These are really just required to help out the parser,
  // and don't convey much independent information.  A group of these with no
  // other distinguishing characters can't really be considered to have any
  // information in it.
  array['('] = array[')'] = array['{'] = array['}'] = array[',']
             = array['.'] = array['#'] = array['/'] = array['*']
             = array['"'] = array[';'] = array['\''] = 0;
  // Operators and characters used to build expressions -- these are fairly
  // information-dense, and so score highly.
  array['!'] = array['%'] = array['^'] = array['&']
             = array['['] = array[']'] = array['?'] = array['\\']
             = array['|'] = array['<'] = array['>'] = array['+']
             = array['-'] = array['='] = array['~'] = array['@']
             = array['`'] = 5;
  // Slot 0 is the default score added to all entries.
  // We set this to 0; no amount of information unless there are real chars
  // in the string.
  array[0] = 0;

  return array;
}

// Calculate a score for how much "information" these is in this string.
// If a certain matched region has too low of a score, it may not be
// considered an actual match.
// We currently just give a certain number of points for each character
// in the string, based on how "informative" we deem that character
// (see GenerateScores above).
static int CalculateScore(const char* data,
                          int length,
                          const int* score_matrix) {
  int score = score_matrix[0];
  for (int i = 0; i < length; ++i) {
    unsigned char c = static_cast<unsigned char>(data[i]);
    score += score_matrix[c];
  }
  return score;
}

ProcessedEntry::ProcessedEntry(const char* d, int len, int line_no)
    : data(d), length(len), number(line_no) {
  score = CalculateScore(d, length, DefaultScoreMatrix());
}

ProcessedEntry::ProcessedEntry(const char* d, int len, int line_no,
                               const int* score_matrix)
    : data(d), length(len), number(line_no) {
  score = CalculateScore(d, length, score_matrix);
}

int ProcessedEntry::BoundaryScore() const {
  // Blank or nullptr entry -- score of 0, plus -3 bonus.
  if (length <= 0) return -3;
  // Another way of getting a blank line...
  if (length == 1 && data[0] == '\n') return -3;
  // Yet another blank line...
  if (length == 2 && data[0] == '\r' && data[1] == '\n') return -3;
  return score;
}

char ProcessedEntry::LastRealChar() const {
  if (length <= 0) return '\0';
  if (length == 1) return data[0];
  if (data[length - 1] == '\n') {
    // If this is CRLF, get the character before, if possible.
    if (length >= 3 && data[length - 2] == '\r') return data[length - 3];
    return data[length - 2];
  }
  return data[length - 1];
}

int ProcessedEntry::ProcessString(absl::string_view input,
                                  std::list<ProcessedEntry>* out) {
  return ProcessString(input, out, DefaultScoreMatrix());
}

int ProcessedEntry::ProcessString(absl::string_view input,
                                  std::list<ProcessedEntry>* out,
                                  const int* score_matrix) {
  int size = 0;
  // We split into lines, and count characters per line.
  int begin_index = 0;  // index where this line began
  int line_no = 0;      // line number for each entry
  for (size_t i = 0; i < input.size(); ++i) {
    if (input[i] == '\n') {
      out->push_back(ProcessedEntry(input.data() + begin_index,
                                    i - begin_index + 1,
                                    line_no++,
                                    score_matrix));
      ++size;
      begin_index = i + 1;
    }
  }
  // Handle the last chunk, if there was no trailing \n
  if (begin_index < static_cast<int>(input.size())) {
    out->push_back(ProcessedEntry(input.data() + begin_index,
                                  input.size() - begin_index,
                                  line_no,
                                  score_matrix));
    ++size;
  }
  return size;
}

int ProcessedEntry::ProcessVector(const std::vector<const char *>& input,
                                  std::list<ProcessedEntry>* out,
                                  const int* score_matrix) {
  for (size_t i = 0; i < input.size(); ++i) {
    out->push_back(ProcessedEntry(
                       input[i], strlen(input[i]),
                       i,
                       score_matrix));
  }
  return input.size();
}

int ProcessedEntry::ProcessVectorOfStrings(
    const std::vector<std::string>& input, std::list<ProcessedEntry>* out,
    const int* score_matrix) {
  for (size_t i = 0; i < input.size(); ++i) {
    out->push_back(ProcessedEntry(
        input[i].data(), input[i].size(), i, score_matrix));
  }
  return input.size();
}

int ProcessedEntry::ProcessVectorOfStringViews(
    const std::vector<absl::string_view>& input, std::list<ProcessedEntry>* out,
    const int* score_matrix) {
  for (size_t i = 0; i < input.size(); ++i) {
    out->push_back(
        ProcessedEntry(input[i].data(), input[i].size(), i, score_matrix));
  }
  return input.size();
}

ReDiff::ReDiff()
    : null_entry_(),
      left_size_(0),
      right_size_(0),
      tolerance_(-1),
      score_matrix_(ProcessedEntry::DefaultScoreMatrix(),
                    ProcessedEntry::DefaultScoreMatrix() + 256) {}

// Diff with inputs as strings--we break the inputs into lines,
// then execute diff on the result.
void ReDiff::DiffStrings(absl::string_view left, absl::string_view right) {
  left_list_.clear();
  right_list_.clear();

  left_size_ = ProcessedEntry::ProcessString(
      left, &left_list_, &score_matrix_[0]);
  right_size_ = ProcessedEntry::ProcessString(
      right, &right_list_, &score_matrix_[0]);
  Diff();
}

// Diff with inputs as vectors of strings.  This makes the assumption that
// you have already broken the input into logical lines, and thus does not
// attempt to further partition your input.
void ReDiff::DiffVectors(const std::vector<const char *>& left,
                         const std::vector<const char *>& right) {
  left_list_.clear();
  right_list_.clear();
  left_size_ = ProcessedEntry::ProcessVector(
      left, &left_list_, &score_matrix_[0]);
  right_size_ = ProcessedEntry::ProcessVector(
      right, &right_list_, &score_matrix_[0]);
  Diff();
}

void ReDiff::DiffVectorsOfStrings(const std::vector<std::string>& left,
                                  const std::vector<std::string>& right) {
  left_list_.clear();
  right_list_.clear();
  left_size_ = ProcessedEntry::ProcessVectorOfStrings(
      left, &left_list_, &score_matrix_[0]);
  right_size_ = ProcessedEntry::ProcessVectorOfStrings(
      right, &right_list_, &score_matrix_[0]);
  Diff();
}

void ReDiff::DiffVectorsOfStringViews(
    const std::vector<absl::string_view>& left,
    const std::vector<absl::string_view>& right) {
  left_list_.clear();
  right_list_.clear();
  left_size_ = ProcessedEntry::ProcessVectorOfStringViews(left, &left_list_,
                                                          &score_matrix_[0]);
  right_size_ = ProcessedEntry::ProcessVectorOfStringViews(right, &right_list_,
                                                           &score_matrix_[0]);
  Diff();
}

void ReDiff::Diff() {
  FILE_BASED_TEST_DRIVER_DCHECK_EQ(static_cast<int>(left_list_.size()), left_size_);
  FILE_BASED_TEST_DRIVER_DCHECK_EQ(static_cast<int>(right_list_.size()), right_size_);
  left_matches_.clear();
  right_matches_.clear();
  for (int i = 0; i < left_size_; ++i) {
    left_matches_.push_back(std::make_pair(UNMATCHED, i));
  }
  for (int i = 0; i < right_size_; ++i) {
    right_matches_.push_back(std::make_pair(UNMATCHED, i));
  }

  // Insert nullptr entries at the beginning and end of the input.  These will
  // never match anything, and guarantee that we never accidentally iterate
  // off the end of the list when doing simple greedy matching.
  left_list_.push_back(null_entry_);
  left_list_.push_front(null_entry_);
  right_list_.push_back(null_entry_);
  right_list_.push_front(null_entry_);
  left_size_ += 2;
  right_size_ += 2;
  // Start performing Diff iterations.
  DiffIteration(tolerance_);

  // Okay, we're done.  left_matches_ and right_matches_ should contain
  // all the information we need to calculate diff regions.
  std::vector<DiffChunk> left_chunks;
  std::vector<DiffChunk> right_chunks;

  Chunkify(left_matches_, &left_chunks, REMOVED);
  Chunkify(right_matches_, &right_chunks, ADDED);

  // Perform chunk conversions as necessary, and copy
  // them to our internal chunk vector.
  chunks_.clear();
  ConvertChunks(&left_chunks, &right_chunks, &chunks_);
}

// Do one iteration of calling LCS and determining matches.
// Only count regions as matches if they are above "tolerance"
// in score, and label matches as match_type.
// Return the number of matched regions.
int ReDiff::DiffIteration(int tolerance) {
  // Before calling LCS, look for leading or trailing matches, which we can
  // process in linear time.  This reduces the non-linear portion of the diff.
  // Note: these iterators are required later to let us delete matched
  // elements from the list.
  LPEit left_it = left_list_.begin();
  LPEit right_it = right_list_.begin();
  int leading_matches = ProcessLeadingMatches(&left_it, &right_it);
  // We don't use reverse_iterator here, because splice() doesn't work with
  // them.  They're bi-directional, though, so we just use -- instead of ++
  // in ProcessTrailingMatches.
  LPEit left_reverse_it = left_list_.end();
  LPEit right_reverse_it = right_list_.end();
  int trailing_matches = ProcessTrailingMatches(
      &left_reverse_it, &right_reverse_it, leading_matches);

  // left_it/right_it now point to the first unmatched entry in each list,
  // or the trailing pad if the two are equal.
  // If trailing_matches > 0, the reverse_its point just past the last
  // unmatched entry in each list.  (Could be the same entry as left/right_it
  // if only one line differs.)
  // Splice out the ranges between these iterators and the respective ends
  // before passing them to LCS.
  std::list<ProcessedEntry> left_header, right_header;
  std::list<ProcessedEntry> left_footer, right_footer;
  if (leading_matches > 0) {
    // Splice out the range from the beginning of the left/right_list_ through
    // left/right_it.  Insert them into the left/right_header.
    left_header.splice(left_header.begin(), left_list_,
                       left_list_.begin(), left_it);
    right_header.splice(right_header.begin(), right_list_,
                        right_list_.begin(), right_it);
    // Modify left/right_size.  We've removed leading_matches entries,
    // plus the pad.
    left_size_ -= leading_matches + 1;
    right_size_ -= leading_matches + 1;
  }
  if (trailing_matches > 0) {
    // Same as above, but for the other end.  Splice into left/right_footer.
    left_footer.splice(left_footer.end(), left_list_,
                       left_reverse_it, left_list_.end());
    right_footer.splice(right_footer.end(), right_list_,
                        right_reverse_it, right_list_.end());
    left_size_ -= trailing_matches + 1;
    right_size_ -= trailing_matches + 1;
  }
  // Now call LCS on parts not matched in the header/footer.
  std::list<DiffMatch> result;
  WrapLCS2(lcs_options_, left_list_, right_list_, &result);
  // Before looping over the match list, reinsert the header and footer, and
  // insert DiffMatch entries for them.
  if (leading_matches > 0) {
    left_list_.splice(left_list_.begin(), left_header);
    right_list_.splice(right_list_.begin(), right_header);
    // Restore left/right_size_
    left_size_ += leading_matches + 1;
    right_size_ += leading_matches + 1;
    // Prepend a DiffMatch for the header.
    // It starts at offset 1 (just beyond the pad) for both sides.
    // Note that we don't allow this match to be discarded due to its score.
    result.push_front(DiffMatch(1, 1, leading_matches, true));
  }
  // header_offset is 0 if there are no leading matches, and leading_matches+1
  // otherwise -- it's the number of entries that were spliced out of the
  // front of the list above, and therefore the amount that DiffMatch entries
  // need their indices adjusted by.
  int header_offset = leading_matches > 0 ? leading_matches + 1 : 0;
  if (trailing_matches > 0) {
    left_list_.splice(left_list_.end(), left_footer);
    right_list_.splice(right_list_.end(), right_footer);
    // Restore left/right_size_ -- must be done before using them below.
    left_size_ += trailing_matches + 1;
    right_size_ += trailing_matches + 1;
    // Append a match for the footer.
    // The pad is at index (size - 1), and our match ends just before the pad.
    // The match therefore begins at index (size - 1 - trailing_matches).
    // But in the loop in ProcessMatchList, we'll be adding header_offset
    // to all indices, so subtract that from our indices too.
    int total_offset = header_offset + trailing_matches + 1;
    result.push_back(DiffMatch(left_size_ - total_offset,
                               right_size_ - total_offset,
                               trailing_matches,
                               true));
  }
  // Note: the loop in ProcessMatchList has an invariant that the "current"
  // iterator has already had header_offset applied, and it fixes the "next"
  // iterator before looping.  Therefore, the first entry in the list needs
  // to satisfy this invariant before we start.  Two cases:
  // - Leading matches were found.  We inserted an entry at the head of the
  //   list<DiffMatch>, and this entry should not have the offset applied
  //   since it was not generated by LCS.
  // - Leading matches were not found.  In this case, header_offset == 0, so
  //   applying the offset is a no-op.
  // Either way, nothing to do here.

  return ProcessMatchList(&result, header_offset, tolerance);
}

int ReDiff::ProcessLeadingMatches(LPEit* left_begin, LPEit* right_begin) {
  LPEit& left_it = *left_begin;
  LPEit& right_it = *right_begin;
  // Both lists are padded with null entries that never match; move over them.
  ++left_it;
  ++right_it;
  int leading_matches = 0;
  while (*left_it == *right_it) {
    ++leading_matches;
    ++left_it;
    ++right_it;
  }
  return leading_matches;
}

int ReDiff::ProcessTrailingMatches(LPEit* left_end, LPEit* right_end,
                                   int leading_matches) {
  // As noted above, these are not reverse_iterators, but we're iterating
  // backwards: hence the use of -- instead of ++ here.
  LPEit& left_reverse_it = *left_end;
  LPEit& right_reverse_it = *right_end;
  // Don't process trailing matches if leading matches already accounted for
  // the entirety of either file.  (+2 accounts for leading/trailing pad entry)
  // Also, make sure that the leading and trailing matches don't overlap!
  int max_trailing_match = std::min(left_size_ - leading_matches - 2,
                                    right_size_ - leading_matches - 2);
  int trailing_matches = 0;
  // Decrement the iterators once to make them dereferenceable.
  --left_reverse_it;
  --right_reverse_it;
  if (max_trailing_match > 0) {
    // Move past the pad.
    --left_reverse_it;
    --right_reverse_it;
    while (*left_reverse_it == *right_reverse_it &&
           trailing_matches < max_trailing_match) {
      ++trailing_matches;
      --left_reverse_it;
      --right_reverse_it;
    }
    // Increment them one time -- we want them to point to a match, so that
    // splice() behaves correctly in DiffIteration.
    ++left_reverse_it;
    ++right_reverse_it;
  }
  return trailing_matches;
}

int ReDiff::ProcessMatchList(std::list<DiffMatch>* lcs_result,
                             int header_offset, int tolerance) {
  // LCS() reports "line numbers" relative to the list you pass it.  This will
  // not match up with the "actual" line numbers in the file.  We keep counts
  // here, noting the number of times we've incremented our left/right
  // iterators--this lets us align ourselves with what LCS reports.
  // We also have to account for any matched header above, since it was not
  // passed to LCS.  We do this by maintaining the following invariant:
  // - "it" within the body of the loop has already had offsets applied, if
  //   necessary.
  // - "it+1", if a valid iterator, needs to have offsets applied.  We apply
  //   the offsets in the body of the loop to maintain the invariant for
  //   the next iteration.
  // Note that the ProcessedEntries in the list will themselves contain the
  // "real" line numbers, which give us appropriate indices into our master
  // vectors.
  int left_index = 0;
  int right_index = 0;
  // Reset our iterators back to the beginning of the lists.
  LPEit left_it = left_list_.begin();
  LPEit right_it = right_list_.begin();

  // Total number of accepted matched regions (our return value)
  int matches = 0;

  for (std::list<DiffMatch>::iterator it = lcs_result->begin();
       it != lcs_result->end();
       ++it) {
    int match_length = it->length;
    // Advance our iterators until we hit the index LCS reports.
    while (left_index < it->left_start) {
      ++left_index;
      ++left_it;
    }
    while (right_index < it->right_start) {
      ++right_index;
      ++right_it;
    }
    // Now *left_it == *right_it, and the next <match_length> slots match too.

    // Before considering this a match (or even testing its score), test for
    // "slidability" of the region.  If it's slidable, we need to consider
    // the slide before touching this match.
    // See comment on SlideRegion() in rediff.h for more details on sliding.
    //
    // Note that the "slidable" section is the set of *unmatched* lines in
    // between two matches.  We're looking for an insert or delete (a CHANGE
    // is never slidable).  This means that, for two adjacent matches, either
    // it->left_start + match_length == next->left_line (insert), or
    // it->right_start + match_length == next->right_line (delete).
    std::list<DiffMatch>::iterator next_it = it;
    ++next_it;
    if (next_it != lcs_result->end()) {
      // Apply offsets to the next iterator so that the pair of iterators
      // acted on by SlideRegion will both have had the offset applied.
      next_it->left_start += header_offset;
      next_it->right_start += header_offset;
      if (it->left_start + match_length == next_it->left_start) {
        // Potentially slidable insert.
        SlideRegion(right_it, &it, &next_it,
                    next_it->right_start - it->right_start);
        // Recompute match_length in case it changed.
        match_length = it->length;
      } else if (it->right_start + match_length == next_it->right_start) {
        // Potentially slidable delete.
        SlideRegion(left_it, &it, &next_it,
                    next_it->left_start - it->left_start);
        // Recompute match_length in case it changed.
        match_length = it->length;
      }
    }
    // Iterator copies to use for region matcihng
    LPEit lit = left_it;
    LPEit rit = right_it;

    // Rejecting certain matches (particularly blank lines) can sometimes
    // result in some unmatched trailing lines.  We remedy this by doing
    // a quick backwards seek from the beginning of a matched region.
    while (true) {
      LPEit lprev = lit;
      LPEit rprev = rit;
      --lprev, --rprev;
      if (!(*lprev == *rprev))
        break;  // lines don't match
      if (lprev->number + 1 != lit->number ||
          rprev->number + 1 != rit->number)
        break;  // lines aren't adjacent
      if (left_matches_[lprev->number].first != UNMATCHED ||
          right_matches_[rprev->number].first != UNMATCHED)
        break;  // lines were matched somewhere else
      // We have a winner.
      --lit, --rit;
      ++match_length;
    }
    if (match_length == 0) continue;  // can happen if we SlideRegion().

    // Calculate the score for this matched region
    int score = 0;
    LPEit score_it = lit;
    for (int i = 0; i < match_length && score <= tolerance; ++i) {
      score += score_it->score;
      ++score_it;
    }
    if (score > tolerance || it->ignore_score) {
      // We intend to accept this region.
      ++matches;

      // Reset left_it and right_it to the beginning of the region.
      left_it = lit;
      right_it = rit;
      // Mark each line in this region as a match
      for (int i = 0; i < match_length; ++i, ++rit, ++lit) {
        left_matches_[lit->number].first = MATCHED;
        left_matches_[lit->number].second = rit->number;
        right_matches_[rit->number].first = MATCHED;
        right_matches_[rit->number].second = lit->number;
      }
      // Erase all of the list entries that were matched.
      // left/right_it mark the beginning of the region, lit/rit have been
      // advanced just past the end of the region.
      left_it = left_list_.erase(left_it, lit);
      right_it = right_list_.erase(right_it, rit);
      // Insert a nullptr ProcessedEntry into the list, which will never equal
      // any other ProcessedEntry.  This prevents a later match from occurring
      // across the gap we've just created.
      // This won't impact left/right_index, since they only count forwards,
      // and use the indices that were referenced when the list<DiffMatch> was
      // created.
      left_list_.insert(left_it, null_entry_);
      right_list_.insert(right_it, null_entry_);
      // Update left/right_size to match the new size of the list.
      left_size_ -= match_length - 1;
      right_size_ -= match_length - 1;
      // left/right_index were never moved backwards, so use the original
      // match length (it->length), not match_length here.
      left_index += it->length;
      right_index += it->length;
    }
  }
  return matches;
}

// SlideRegion -- try to slide some unmatched text forward or backwards.
void ReDiff::SlideRegion(const LPEit& text_it,
                         std::list<DiffMatch>::iterator* current_match_param,
                         std::list<DiffMatch>::iterator* next_match_param,
                         int gap_length) {
  // De-pointerize these parameters since we like to apply operator-> to
  // iterators.
  std::list<DiffMatch>::iterator& current_match = *current_match_param;
  std::list<DiffMatch>::iterator& next_match = *next_match_param;
  // First things first: determine more LPEits that we need.  One to the
  // beginning of the "extra" text, and one to the beginning of the next
  // matched region.
  LPEit extra_begin = text_it;
  for (int i = 0; i < current_match->length; ++i, ++extra_begin) {}
  LPEit next_begin = text_it;
  for (int i = 0; i < gap_length; ++i, ++next_begin) {}

  // Determine the maximum amount we can slide in either direction.
  int max_backwards_slide = 0;
  LPEit slide_top = extra_begin;
  LPEit slide_bottom = next_begin;
  for (int i = 0; i < current_match->length; ++i, ++max_backwards_slide) {
    --slide_top;
    --slide_bottom;
    if (*slide_top != *slide_bottom) {
      // Can't slide this far--the entries aren't equal.
      break;
    }
  }
  int max_forwards_slide = 0;
  slide_top = extra_begin;
  slide_bottom = next_begin;
  for (int i = 0; i < next_match->length; ++i, ++max_forwards_slide) {
    if (*slide_top != *slide_bottom) {
      // Can't slide this far--the entries aren't equal.
      break;
    }
    ++slide_top;
    ++slide_bottom;
  }
  // Bail if sliding isn't possible.
  if (max_backwards_slide == 0 && max_forwards_slide == 0) return;

  // We need to score all possible slides and select the best one.
  int n_slides = max_backwards_slide + max_forwards_slide + 1;
  // 4 iterators for helping us score the boundaries; the "before" and "after"
  // iterators at each boundary.
  LPEit before_top_boundary = extra_begin;
  LPEit after_top_boundary = extra_begin;
  --before_top_boundary;
  LPEit before_bottom_boundary = next_begin;
  LPEit after_bottom_boundary = next_begin;
  --before_bottom_boundary;
  for (int i = 0; i < max_backwards_slide; ++i,
           --before_top_boundary, --after_top_boundary,
           --before_bottom_boundary, --after_bottom_boundary) {
  }
  std::vector<int> scores(n_slides);
  for (int i = 0; i < n_slides; ++i,
           ++before_top_boundary, ++after_top_boundary,
           ++before_bottom_boundary, ++after_bottom_boundary) {
    // Scoring: this is ultimately what determines which slide we select.
    // The end goal is to make the diff "semantically valid."  This is a
    // very subjective goal.  We score regions by checking the text at the
    // boundaries--the last fragment in the "matched" section and the first
    // fragment in the "insert/delete" section (or vice versa at the bottom
    // end).  Our assumption is that it's more semantically valid to declare
    // boundaries at whitespace, blank lines, C++ "block" boundaries, etc.
    //
    // Note that low scores are better, so subtraction is a "bonus" and
    // addition is a "penalty".


    // - The upper boundary is given a bonus of -2 if the last character
    //   before the boundary is '}' (or "}\n")
    // - The upper boundary is given a bonus of -2 if the last character on
    //   the line after the boundary is '{' (or "{\n")
    // - The lower boundary is given a bonus of -2 if the last character before
    //   the boundary is '}' (or "}\n").
    // - A slide's initial score is the sum of the scores of its two
    //   boundaries.
    // - A slide is given an additional -10 bonus if the slide completely
    //   consumes the "equal" chunk on either side of it.

    // Initial score per entry is the "score" field on the entry, plus:
    // - Bonus of -3 for an entry that is blank or contains only '\n'
    int top_score_1 = before_top_boundary->BoundaryScore();
    int top_score_2 = after_top_boundary->BoundaryScore();
    int bottom_score_1 = before_bottom_boundary->BoundaryScore();
    int bottom_score_2 = after_bottom_boundary->BoundaryScore();
    // Boundaries are given the lower of the two scores that generate the
    // boundary.
    int top_boundary_score = std::min(top_score_1, top_score_2);
    int bottom_boundary_score = std::min(bottom_score_1, bottom_score_2);
    // Boundary bonuses: if the line before the upper boundary ends with
    // a '}', give a bonus--it's probably the end of a C++/Java block.
    if (before_top_boundary->LastRealChar() == '}') top_boundary_score -= 2;
    // Similarly, if the line after the upper boundary looks to be starting
    // a block, grant a bonus.
    if (after_top_boundary->LastRealChar() == '{') top_boundary_score -= 2;
    // If the line before the bottom boundary is closing a block,
    // grant a bonus.
    if (before_bottom_boundary->LastRealChar() == '}')
      bottom_boundary_score -= 2;
    // Grant a small bonus if the line before a boundary is smaller than the
    // line after it, since lines that end a logical unit tend to be shorter.
    // This often helps break ties between two possible slides, since the
    // boundaries are given the min of the two contributers.
    if (top_score_1 < top_score_2) top_boundary_score -= 1;
    if (bottom_score_1 < bottom_score_2) bottom_boundary_score -= 1;
    // The slide's score is the sum of the two boundary scores.
    scores[i] = top_boundary_score + bottom_boundary_score;
  }
  // The slide gets bonus points for consuming the entire match on either side.
  if (max_backwards_slide == current_match->length) {
    scores[0] -= 10;
  }
  if (max_forwards_slide == next_match->length) {
    scores[n_slides - 1] -= 10;
  }
  int best_score = std::numeric_limits<int32_t>::max();
  int best_index = -1;
  for (int i = 0; i < n_slides; ++i) {
    if (scores[i] < best_score) {
      best_score = scores[i];
      best_index = i;
    }
  }
  // Logical slide: -1 means "backwards 1", etc.
  int logical_slide = best_index - max_backwards_slide;
  // If the best slide is the one we have, bail with no change.
  if (logical_slide == 0) return;
  // Perform an actual slide!
  // Note that these expressions work for either a positive or negative logical
  // slide.

  // current match is longer if we're sliding forwards.
  current_match->length += logical_slide;
  // next_match starts later if we're sliding forwards.
  next_match->left_start += logical_slide;
  next_match->right_start += logical_slide;
  // next_match is shorter if we're sliding forwards.
  next_match->length -= logical_slide;
}

// Takes the information from a match vector @p matches and convert it into
// a chunk list @p chunks.
void ReDiff::Chunkify(const std::vector<std::pair<MatchType, int> >& matches,
                      std::vector<DiffChunk>* chunks,
                      ChunkType unmatched_type) {
  chunks->clear();
  if (matches.empty()) return;

  MatchType last_type = matches[0].first;
  int first_line = matches[0].second;
  int last_line = matches[0].second;
  int first_index = 0;
  int last_index = 0;

  for (size_t i = 1; i < matches.size(); ++i) {
    if (matches[i].first == last_type &&
        matches[i].second == last_line + 1) {
      // This match is the same type as the last one, and the line number is
      // 1 higher, meaning this continues a single chunk.
      last_line++;
      last_index = i;
    } else {
      // We're starting a new chunk.  Emit the previous one.
      chunks->push_back(DiffChunk());
      DiffChunk& c = chunks->back();
      c.first_line = first_index;
      c.last_line = last_index;
      c.source_first = first_line;
      c.source_last = last_line;
      if (last_type == MATCHED)
        c.type = UNCHANGED;
      else if (last_type == UNMATCHED)
        c.type = unmatched_type;
      else
        FILE_BASED_TEST_DRIVER_LOG(FATAL) << "Invalid chunk type: " << last_type;

      // Start a new chunk here
      first_index = last_index = i;
      first_line = last_line = matches[i].second;
      last_type = matches[i].first;
    }
  }
  // Emit the final chunk.
  DiffChunk c;
  c.first_line = first_index;
  c.last_line = last_index;
  c.source_first = first_line;
  c.source_last = last_line;
  if (last_type == MATCHED) c.type = UNCHANGED;
  else if (last_type == UNMATCHED) c.type = unmatched_type;
  chunks->push_back(c);
}

// Take our two vectors of "raw" chunks, left_chunks and right_chunks,
// and convert them into a single unified list of chunks that expresses the
// difference between the two files.  This performs things such as matching
// up a REMOVED chunk on the left with an ADDED chunk on the right, and marking
// them as CHANGED.  At the end, we interleave the chunks from both sides
// appropriately to get the end result.
void ReDiff::ConvertChunks(std::vector<DiffChunk>* left_chunks,
                           std::vector<DiffChunk>* right_chunks,
                           std::vector<DiffChunk>* final_chunks) {
  final_chunks->clear();

  // chunks_to_add is a map of indices in right_chunks to indices in
  // left_chunks.  The idea is that after emitting right_chunks[first],
  // we should emit left_chunks[second] (with type REMOVED).  This allows
  // removed regions to be displayed in the appropriate context in the final
  // diff. (case 2 above)
  std::map<int, int> chunks_to_add;
  // If we have something like the following...
  // - file 1, lines 1-10 REMOVED
  // - file 2, lines 1-12 ADDED
  // ...we'd like to convert it to "lines 1-10 changed to lines 1-12."
  // We build up a list of added/removed and matched chunks.  Every add/remove
  // pair we find between associated match chunks can be converted to a CHANGE.
  // Note: this must be done after the above, since the above can modify some
  // added/removed chunks into different types.
  std::vector<std::pair<ChunkType, int> > left_candidates;
  for (size_t i = 0; i < left_chunks->size(); ++i) {
    if (left_chunks->at(i).type == REMOVED ||
        left_chunks->at(i).type == UNCHANGED)
      left_candidates.push_back(std::make_pair(left_chunks->at(i).type, i));
  }
  std::vector<std::pair<ChunkType, int> > right_candidates;
  for (size_t i = 0; i < right_chunks->size(); ++i) {
    if (right_chunks->at(i).type == ADDED ||
        right_chunks->at(i).type == UNCHANGED)
      right_candidates.push_back(std::make_pair(right_chunks->at(i).type, i));
  }
  size_t i = 0;
  size_t j = 0;
  while (i < left_candidates.size() && j < right_candidates.size()) {
    if (left_candidates[i].first == REMOVED &&
        right_candidates[j].first == ADDED) {
      int left_index = left_candidates[i].second;
      int right_index = right_candidates[j].second;

      left_chunks->at(left_index).type = CHANGED;

      right_chunks->at(right_index).type = CHANGED;
      right_chunks->at(right_index).source_first =
        left_chunks->at(left_index).first_line;
      right_chunks->at(right_index).source_last =
        left_chunks->at(left_index).last_line;
      ++i, ++j;
    } else if (left_candidates[i].first == UNCHANGED &&
               right_candidates[j].first == UNCHANGED) {
      ++i, ++j;
    } else if (left_candidates[i].first == UNCHANGED) {
      ++j;
    } else if (right_candidates[j].first == UNCHANGED) {
      ++i;
    } else {
      FILE_BASED_TEST_DRIVER_LOG(FATAL) << "Internal error converting add/remove chunks to changes.";
    }
  }

  // Now build up our final list of chunks.
  // The list is primarily composed of the elements of right_chunks, but
  // we add:
  // - Elements from chunks_to_add
  // - Any REMOVED elements we can find in left_chunks.
  i = 0;
  j = 0;
  // Next chunks_to_add chunk we'll be adding.
  std::map<int, int>::const_iterator map_it = chunks_to_add.begin();

  while (true) {
    if (i >= left_chunks->size() && j >= right_chunks->size()) break;
    // Process all entries from right_chunks up to the next CHANGED/UNCHANGED
    // chunk, or EOF.
    while (j < right_chunks->size() &&
           right_chunks->at(j).type != UNCHANGED &&
           right_chunks->at(j).type != CHANGED) {
      if (right_chunks->at(j).type != IGNORED)
        final_chunks->push_back(right_chunks->at(j));
      if (map_it != chunks_to_add.end() &&
          map_it->first == static_cast<int>(j)) {
        DiffChunk c = left_chunks->at(map_it->second);
        // This is a chunk from the left, so its lines aren't what we want.
        // Swap them around.  Then change type to REMOVED (it was set to
        // IGNORED earlier).
        c.source_first = c.first_line;
        c.source_last = c.last_line;
        c.first_line = c.last_line = 0;
        c.type = REMOVED;
        final_chunks->push_back(c);
        ++map_it;
      } else if (map_it != chunks_to_add.end() &&
                 map_it->first < static_cast<int>(j)) {
        FILE_BASED_TEST_DRIVER_LOG(FATAL) << "Internal error: missed chance to insert chunk at "
                   << map_it->first << " (current index = " << j << ")";
      }
      ++j;
    }
    // Process all entries from left_chunks up to the next CHANGED/UNCHANGED
    // chunk, or EOF.
    while (i < left_chunks->size() &&
           left_chunks->at(i).type != UNCHANGED &&
           left_chunks->at(i).type != CHANGED) {
      if (left_chunks->at(i).type == REMOVED) {
        final_chunks->push_back(left_chunks->at(i));
      }
      ++i;
    }
    // Emit the changed/unchanged chunk, if one exists.
    if (j < right_chunks->size()) {
      final_chunks->push_back(right_chunks->at(j));
      ++i, ++j;
    }
  }
}

// Write our list of chunks to a string.
void ReDiff::ChunksToString(std::string* out) const {
  out->clear();
  std::vector<DiffChunk> v;
  ChunksToVector(&v);
  for (size_t i = 0; i < chunks_.size(); ++i) {
    absl::StrAppendFormat(out, "%s %d %d %d %d\n", v[i].opcode(),
                          v[i].source_first, v[i].source_last, v[i].first_line,
                          v[i].last_line);
  }
}

// Write our list of chunks to a provided vector.
void ReDiff::ChunksToVector(std::vector<DiffChunk>* v) const {
  for (size_t i = 0; i < chunks_.size(); ++i) {
    v->push_back(chunks_[i]);
    DiffChunk& c = v->back();
    // We count our lines a little differently from python's difflib, so
    // we have to do some conversions.  In particular, we like to give ranges
    // as [x, y] but python expects [x, y).  Also need to null out the
    // opposite lines for ADDED and REMOVED.
    if (c.type == UNCHANGED || c.type == CHANGED) {
      c.last_line++;
      c.source_last++;
    }
    if (c.type == ADDED) {
      c.last_line++;
      c.source_first = c.source_last = 0;
    }
    if (c.type == REMOVED) {
      c.first_line = c.last_line = 0;
      c.source_last++;
    }
  }
}

// Comparator thunk for index_of
struct CmpDiffIndex {
  bool operator()(const DiffChunk& a, const DiffChunk& b) const {
    return a.last_line < b.last_line;
  }
};

// Return the vector index within v where a chunk
// ending with line_number is found.
// Throws a fit and dies if it can't find one.
// This assumes v's chunks to be sorted based on DiffChunk.last_line.
int ReDiff::index_of(int line_number, const std::vector<DiffChunk>& v) {
  CmpDiffIndex c;
  // We compare on last_line, so create a DiffChunk with the appropriate
  // field set.
  DiffChunk chunk;
  chunk.last_line = line_number;
  std::vector<DiffChunk>::const_iterator it =
      std::lower_bound(v.begin(), v.end(), chunk, c);
  // Lower bound just gives us a lower bound where line_number could be
  // inserted; we need to verify that the line number of this lower bound
  // is exactly equal to the value we expect.
  FILE_BASED_TEST_DRIVER_CHECK(it != v.end()) << "index_of: entry not found.";
  FILE_BASED_TEST_DRIVER_CHECK_EQ(it->last_line, line_number) << "index_of: entry not found.";
  // Use iterator subtraction to determine the index.
  return it - v.begin();
}
}  // namespace file_based_test_driver_base

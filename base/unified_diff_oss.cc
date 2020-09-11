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

#include "base/unified_diff_oss.h"

#include <algorithm>
#include <functional>
#include <list>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "base/logging.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "base/diffchunk.h"
#include "base/rediff.h"
#include "re2/re2.h"

namespace file_based_test_driver_base {

namespace {
constexpr char kNoNewlineAtEof[] = "\\ No newline at end of file";

// Identifies whether the line is shared between left and right side, or whether
// it is added or removed.
enum LineType {
  LINE_TYPE_SHARED,
  LINE_TYPE_ADD,
  LINE_TYPE_DELETE,
};

const char* LineTypeToPrefix(const LineType line_type) {
  switch (line_type) {
    case LINE_TYPE_SHARED:
      return " ";
    case LINE_TYPE_ADD:
      return "+";
    case LINE_TYPE_DELETE:
      return "-";
    default:
      return " ";
  }
}

void Print(
    int from, int to,
    const std::vector<file_based_test_driver_base::ProcessedEntry>& lines,
    const UnifiedDiffOptions& options, const LineType line_type,
    std::string* out) {
  const UnifiedDiffColorizer* colorizer = options.colorizer();
  // Do not bother setting colorizer if we are not printing anything.
  if (from >= to) {
    colorizer = nullptr;
  }

  // Prepare the colorization, if applicable.
  const std::string* prefix = nullptr;
  const std::string* suffix = nullptr;
  if (colorizer != nullptr) {
    if (line_type == LINE_TYPE_ADD) {
      prefix = &colorizer->add_prefix();
      suffix = &colorizer->add_suffix();
    } else if (line_type == LINE_TYPE_DELETE) {
      prefix = &colorizer->del_prefix();
      suffix = &colorizer->del_suffix();
    }
  }

  // Prepend the colorization prefix.
  if (prefix != nullptr) {
    absl::StrAppend(out, *prefix);
  }

  // Print the actual lines.
  for (int i = from; i < to; ++i) {
    absl::StrAppend(out, LineTypeToPrefix(line_type));

    auto line = absl::string_view(lines[i].data, lines[i].length);
    if (colorizer != nullptr && colorizer->escaper()) {
      absl::StrAppend(out, colorizer->escaper()(line));
    } else {
      absl::StrAppend(out, line);
    }

    if (i == lines.size() - 1) {
      if (line[line.size() - 1] != '\n') {
        if (options.warn_missing_eof_newline()) {
          absl::StrAppend(out, "\n", kNoNewlineAtEof, "\n");
        } else {
          absl::StrAppend(out, "\n");
        }
      }
    }
  }

  // Append the colorization suffix.
  if (suffix != nullptr) {
    absl::StrAppend(out, *suffix);
  }
}

std::pair<int, int> GetHunkContextSize(int parts, int first_line, int last_line,
                                       int context) {
  first_line = std::max(first_line - context, 0);
  last_line = std::min(last_line + context, parts);
  int length = last_line - first_line;
  if (length > 0) {
    return std::make_pair(first_line + 1, length);
  } else {
    return std::make_pair(0, 0);
  }
}

static std::string FormatLineNumbers(int start, int length) {
  if (length == 1) {
    return absl::StrCat(start);
  } else {
    return absl::StrFormat("%d,%d", start, length);
  }
}

void FlushHunk(
    const std::list<file_based_test_driver_base::DiffChunk>& hunk,
    const std::vector<file_based_test_driver_base::ProcessedEntry>& left,
    const std::vector<file_based_test_driver_base::ProcessedEntry>& right,
    const UnifiedDiffOptions& options, std::string* out) {
  const auto left_info =
      GetHunkContextSize(left.size(), hunk.front().source_first,
                         hunk.back().source_last, options.context_size());
  const auto right_info =
      GetHunkContextSize(right.size(), hunk.front().first_line,
                         hunk.back().last_line, options.context_size());
  absl::StrAppend(
      out, "@@ -", FormatLineNumbers(left_info.first, left_info.second), " +",
      FormatLineNumbers(right_info.first, right_info.second), " @@\n");
  int prev_line = (left_info.first > 0) ? left_info.first - 1 : 0;
  for (const auto& part : hunk) {
    Print(prev_line, part.source_first, left, options, LINE_TYPE_SHARED, out);
    Print(part.source_first, part.source_last, left, options, LINE_TYPE_DELETE,
          out);
    Print(part.first_line, part.last_line, right, options, LINE_TYPE_ADD, out);
    prev_line = part.source_last;
  }
  Print(prev_line,
        std::min(static_cast<int>(left.size()),
                 prev_line + options.context_size()),
        left, options, LINE_TYPE_SHARED, out);
}

void ProcessString(
    absl::string_view s,
    std::vector<file_based_test_driver_base::ProcessedEntry>* out) {
  std::list<file_based_test_driver_base::ProcessedEntry> entries;
  file_based_test_driver_base::ProcessedEntry::ProcessString(s, &entries);
  out->assign(entries.begin(), entries.end());
}

}  // namespace

const UnifiedDiffColorizer* UnifiedDiffColorizer::AnsiColorizer() {
  static const UnifiedDiffColorizer* kAnsiColorizer = new UnifiedDiffColorizer(
      "\033[32m", "\033[0m", "\033[31m", "\033[0m", nullptr);
  return kAnsiColorizer;
}

std::string UnifiedDiff(absl::string_view left, absl::string_view right,
                        absl::string_view left_name,
                        absl::string_view right_name,
                        const UnifiedDiffOptions& options) {
  if (left.empty() && right.empty()) {
    return "";
  }

  file_based_test_driver_base::ReDiff differ;

  std::vector<file_based_test_driver_base::ProcessedEntry> left_entries,
      right_entries;
  ProcessString(left, &left_entries);
  ProcessString(right, &right_entries);
  for (const auto& e : left_entries) {
    differ.push_left(e);
  }
  for (const auto& e : right_entries) {
    differ.push_right(e);
  }
  differ.Diff();
  std::vector<file_based_test_driver_base::DiffChunk> chunks;
  differ.ChunksToVector(&chunks);
  if (chunks.size() == 1 &&
      chunks.front().type == file_based_test_driver_base::UNCHANGED) {
    return "";
  }
  std::string out;
  absl::StrAppend(&out, "--- ", left_name, "\n");
  absl::StrAppend(&out, "+++ ", right_name, "\n");
  file_based_test_driver_base::DiffChunk prev;
  std::list<file_based_test_driver_base::DiffChunk> hunk;
  for (const auto& c : chunks) {
    if (c.type == file_based_test_driver_base::UNCHANGED) {
      if (c.source_last - c.source_first > options.context_size() * 2 &&
          !hunk.empty()) {
        FlushHunk(hunk, left_entries, right_entries, options, &out);
        hunk.clear();
      }
    } else {
      hunk.push_back(c);
      // For ADDED and REMOVED diff chunks, the opposite side doesn't have
      // source lines set. Since we discard UNCHANGED chunks, we simply copy
      // the last line of the previous chunk. Diff chunks are half-open ranges,
      // setting both start and end to the same value results in an empty chunk.
      if (c.type == file_based_test_driver_base::ADDED) {
        hunk.back().source_first = hunk.back().source_last = prev.source_last;
      }
      if (c.type == file_based_test_driver_base::REMOVED) {
        hunk.back().first_line = hunk.back().last_line = prev.last_line;
      }
    }
    prev = c;
  }
  if (!hunk.empty()) {
    FlushHunk(hunk, left_entries, right_entries, options, &out);
  }
  return out;
}

}  // namespace file_based_test_driver_base

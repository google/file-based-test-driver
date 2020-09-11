//
// Copyright 2020 Google LLC
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

#include "base/lcs_util.h"

#include <vector>

#include "gtest/gtest.h"
#include "base/lcs.h"

namespace file_based_test_driver_base {

bool Equals(const Chunk& chunk, int left, int right, int len) {
  return chunk.left == left && chunk.right == right && chunk.length == len;
}

TEST(CanBeMerged, Order) {
  EXPECT_TRUE(CanBeMerged(Chunk(1, 11, 3), Chunk(4, 14, 8)));
  // The second Chunk must appear after the first Chunk.
  EXPECT_FALSE(CanBeMerged(Chunk(4, 14, 8), Chunk(1, 11, 3)));
}

TEST(CanBeMerged, ChunksWithGap) {
  EXPECT_FALSE(CanBeMerged(Chunk(1, 11, 2), Chunk(4, 14, 8)));
}

TEST(CanBeMerged, OverlappingChunks) {
  EXPECT_FALSE(CanBeMerged(Chunk(1, 11, 4), Chunk(4, 14, 8)));
}

TEST(AppendChunk, Sequence) {
  std::vector<Chunk> chunks;
  AppendChunk(1, 11, 3, &chunks);
  EXPECT_EQ(1, chunks.size());
  EXPECT_TRUE(Equals(chunks[0], 1, 11, 3));

  // Empty chunks should be ignored
  AppendChunk(5, 16, 0, &chunks);
  EXPECT_EQ(1, chunks.size());

  // Append a new chunk with some gap to the previous one.
  AppendChunk(5, 16, 2, &chunks);
  EXPECT_EQ(2, chunks.size());
  EXPECT_TRUE(Equals(chunks.back(), 5, 16, 2));

  // Append a new chunk which can be merged with the previous one.
  AppendChunk(7, 18, 3, &chunks);
  EXPECT_EQ(2, chunks.size());
  EXPECT_TRUE(Equals(chunks.back(), 5, 16, 5));

  // Append a new chunk with a gap on the left side only.
  AppendChunk(11, 21, 2, &chunks);
  EXPECT_EQ(3, chunks.size());
  EXPECT_TRUE(Equals(chunks.back(), 11, 21, 2));

  // Append a new chunk with a gap on the right side only.
  AppendChunk(13, 24, 2, &chunks);
  EXPECT_EQ(4, chunks.size());
  EXPECT_TRUE(Equals(chunks.back(), 13, 24, 2));

  // Append a new chunk overlapping with the previous one.
  AppendChunk(10, 20, 20, &chunks);
  EXPECT_EQ(5, chunks.size());
  EXPECT_TRUE(Equals(chunks.back(), 10, 20, 20));
}

TEST(AppendReverseChunk, Sequence) {
  std::vector<Chunk> chunks;
  AppendReverseChunk(13, 24, 2, &chunks);
  EXPECT_EQ(1, chunks.size());
  EXPECT_TRUE(Equals(chunks.back(), 13, 24, 2));

  // Append a new chunk with a gap on the right side only.
  AppendReverseChunk(11, 21, 2, &chunks);
  EXPECT_EQ(2, chunks.size());
  EXPECT_TRUE(Equals(chunks.back(), 11, 21, 2));

  // Append a new chunk with a gap on the left side only.
  AppendReverseChunk(7, 18, 3, &chunks);
  EXPECT_EQ(3, chunks.size());
  EXPECT_TRUE(Equals(chunks.back(), 7, 18, 3));

  // Append a new chunk which can be merged with the previous one.
  AppendReverseChunk(5, 16, 2, &chunks);
  EXPECT_EQ(3, chunks.size());
  EXPECT_TRUE(Equals(chunks.back(), 5, 16, 5));

  // Empty chunks should be ignored
  AppendReverseChunk(1, 11, 0, &chunks);
  EXPECT_EQ(3, chunks.size());

  // Append a new chunk with some gap to the previous one.
  AppendReverseChunk(1, 11, 3, &chunks);
  EXPECT_EQ(4, chunks.size());
  EXPECT_TRUE(Equals(chunks.back(), 1, 11, 3));

  // Append a new chunk overlapping with the previous one.
  AppendReverseChunk(1, 10, 20, &chunks);
  EXPECT_EQ(5, chunks.size());
  EXPECT_TRUE(Equals(chunks[4], 1, 10, 20));
}

TEST(ReorderReverseChunks, OddNumber) {
  std::vector<Chunk> chunks;
  chunks.push_back(Chunk(40, 40, 4));
  chunks.push_back(Chunk(30, 30, 4));
  chunks.push_back(Chunk(20, 20, 4));
  chunks.push_back(Chunk(10, 10, 4));
  chunks.push_back(Chunk(0, 0, 4));
  ReorderReverseChunks(0, &chunks);
  EXPECT_EQ(5, chunks.size());
  EXPECT_TRUE(Equals(chunks[0], 0, 0, 4));
  EXPECT_TRUE(Equals(chunks[1], 10, 10, 4));
  EXPECT_TRUE(Equals(chunks[2], 20, 20, 4));
  EXPECT_TRUE(Equals(chunks[3], 30, 30, 4));
  EXPECT_TRUE(Equals(chunks[4], 40, 40, 4));
}

TEST(ReorderReverseChunks, EvenNumber) {
  std::vector<Chunk> chunks;
  chunks.push_back(Chunk(30, 30, 4));
  chunks.push_back(Chunk(20, 20, 4));
  chunks.push_back(Chunk(10, 10, 4));
  chunks.push_back(Chunk(0, 0, 4));
  ReorderReverseChunks(0, &chunks);
  EXPECT_EQ(4, chunks.size());
  EXPECT_TRUE(Equals(chunks[0], 0, 0, 4));
  EXPECT_TRUE(Equals(chunks[1], 10, 10, 4));
  EXPECT_TRUE(Equals(chunks[2], 20, 20, 4));
  EXPECT_TRUE(Equals(chunks[3], 30, 30, 4));
}

TEST(ReorderReverseChunks, MergeWithPreviousChunk) {
  std::vector<Chunk> chunks;
  chunks.push_back(Chunk(0, 0, 10));
  chunks.push_back(Chunk(30, 30, 4));
  chunks.push_back(Chunk(20, 20, 4));
  chunks.push_back(Chunk(10, 10, 4));
  ReorderReverseChunks(1, &chunks);
  EXPECT_EQ(3, chunks.size());
  EXPECT_TRUE(Equals(chunks[0], 0, 0, 14));
  EXPECT_TRUE(Equals(chunks[1], 20, 20, 4));
  EXPECT_TRUE(Equals(chunks[2], 30, 30, 4));
}

}  // namespace file_based_test_driver_base

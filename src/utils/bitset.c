/*
 * Copyright 2025 Karesis
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "utils/bitset.h"
#include <assert.h>
#include <stddef.h>
#include <string.h>

#define BITSET_NUM_WORDS(num_bits) (((num_bits) + 63) / 64)

#define BITSET_WORD_INDEX(bit) ((bit) / 64)

#define BITSET_BIT_INDEX(bit) ((bit) % 64)

#define BITSET_WORD_MASK(bit) ((uint64_t)1 << BITSET_BIT_INDEX(bit))

Bitset *
bitset_create(size_t num_bits, Bump *arena)
{

  Bitset *bs = BUMP_ALLOC_ZEROED(arena, Bitset);

  bs->num_bits = num_bits;
  if (num_bits == 0)
  {
    bs->num_words = 0;
    bs->words = NULL;
    return bs;
  }

  bs->num_words = BITSET_NUM_WORDS(num_bits);

  bs->words = BUMP_ALLOC_SLICE_ZEROED(arena, uint64_t, bs->num_words);

  return bs;
}

Bitset *
bitset_create_all(size_t num_bits, Bump *arena)
{

  Bitset *bs = BUMP_ALLOC(arena, Bitset);

  bs->num_bits = num_bits;
  if (num_bits == 0)
  {
    bs->num_words = 0;
    bs->words = NULL;
    return bs;
  }

  bs->num_words = BITSET_NUM_WORDS(num_bits);

  bs->words = BUMP_ALLOC_SLICE(arena, uint64_t, bs->num_words);

  bitset_set_all(bs);

  return bs;
}

void
bitset_set(Bitset *bs, size_t bit)
{
  assert(bit < bs->num_bits && "Bitset index out of bounds");
  bs->words[BITSET_WORD_INDEX(bit)] |= BITSET_WORD_MASK(bit);
}

void
bitset_clear(Bitset *bs, size_t bit)
{
  assert(bit < bs->num_bits && "Bitset index out of bounds");
  bs->words[BITSET_WORD_INDEX(bit)] &= ~BITSET_WORD_MASK(bit);
}

bool
bitset_test(const Bitset *bs, size_t bit)
{
  assert(bit < bs->num_bits && "Bitset index out of bounds");
  return (bs->words[BITSET_WORD_INDEX(bit)] & BITSET_WORD_MASK(bit)) != 0;
}

void
bitset_set_all(Bitset *bs)
{
  if (bs->num_words == 0)
    return;

  size_t full_words = bs->num_words;
  size_t remaining_bits = bs->num_bits % 64;

  if (remaining_bits != 0)
  {

    if (bs->num_words > 0)
    {
      full_words = bs->num_words - 1;
    }

    bs->words[bs->num_words - 1] = ((uint64_t)1 << remaining_bits) - 1;
  }

  for (size_t i = 0; i < full_words; i++)
  {
    bs->words[i] = (uint64_t)-1;
  }
}

void
bitset_clear_all(Bitset *bs)
{
  if (bs->num_words == 0)
    return;
  memset(bs->words, 0, bs->num_words * sizeof(uint64_t));
}

bool
bitset_equals(const Bitset *bs1, const Bitset *bs2)
{
  if (bs1->num_bits != bs2->num_bits)
  {
    return false;
  }
  if (bs1->num_words == 0)
  {
    return true;
  }
  return memcmp(bs1->words, bs2->words, bs1->num_words * sizeof(uint64_t)) == 0;
}

void
bitset_copy(Bitset *dest, const Bitset *src)
{
  assert(dest->num_bits == src->num_bits && "Bitset copy size mismatch");
  if (src->num_words == 0)
    return;
  memcpy(dest->words, src->words, src->num_words * sizeof(uint64_t));
}

void
bitset_intersect(Bitset *dest, const Bitset *src1, const Bitset *src2)
{
  assert(dest->num_bits == src1->num_bits && src1->num_bits == src2->num_bits && "Bitset op size mismatch");
  for (size_t i = 0; i < dest->num_words; i++)
  {
    dest->words[i] = src1->words[i] & src2->words[i];
  }
}

void
bitset_union(Bitset *dest, const Bitset *src1, const Bitset *src2)
{
  assert(dest->num_bits == src1->num_bits && src1->num_bits == src2->num_bits && "Bitset op size mismatch");
  for (size_t i = 0; i < dest->num_words; i++)
  {
    dest->words[i] = src1->words[i] | src2->words[i];
  }
}

void
bitset_difference(Bitset *dest, const Bitset *src1, const Bitset *src2)
{
  assert(dest->num_bits == src1->num_bits && src1->num_bits == src2->num_bits && "Bitset op size mismatch");
  for (size_t i = 0; i < dest->num_words; i++)
  {
    dest->words[i] = src1->words[i] & (~src2->words[i]);
  }
}

size_t
bitset_count_slow(const Bitset *bs)
{

  size_t count = 0;
  for (size_t i = 0; i < bs->num_words; i++)
  {
    uint64_t word = bs->words[i];

    while (word > 0)
    {
      word &= (word - 1);
      count++;
    }
  }

  size_t remaining_bits = bs->num_bits % 64;
  if (remaining_bits != 0 && bs->num_words > 0)
  {
    uint64_t last_word = bs->words[bs->num_words - 1];

    uint64_t invalid_mask = (uint64_t)-1 << remaining_bits;
    uint64_t invalid_bits_set = last_word & invalid_mask;

    while (invalid_bits_set > 0)
    {
      invalid_bits_set &= (invalid_bits_set - 1);
      count--;
    }
  }

  return count;
}
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
#include "utils/bump.h"
#include <stdio.h>
#include <string.h>


#include "test_utils.h"





int
test_create(Bump *arena)
{

  SUITE_START("Bitset: create");


  Bitset *bs1 = bitset_create(100, arena);


  SUITE_ASSERT(bs1 != NULL, "bitset_create returned NULL");
  SUITE_ASSERT(bs1->num_bits == 100, "Expected 100 bits, got %zu", bs1->num_bits);
  SUITE_ASSERT(bs1->num_words == 2, "Expected 2 words (100/64 ceil), got %zu", bs1->num_words);
  SUITE_ASSERT(bs1->words != NULL, "words buffer is NULL");
  SUITE_ASSERT(bs1->words[0] == 0, "words[0] not zero-initialized");
  SUITE_ASSERT(bs1->words[1] == 0, "words[1] not zero-initialized");
  SUITE_ASSERT(bitset_test(bs1, 0) == false, "bit 0 should be false");
  SUITE_ASSERT(bitset_test(bs1, 99) == false, "bit 99 should be false");


  Bitset *bs2 = bitset_create(64, arena);
  SUITE_ASSERT(bs2 != NULL, "bs2 returned NULL");
  SUITE_ASSERT(bs2->num_bits == 64, "Expected 64 bits, got %zu", bs2->num_bits);
  SUITE_ASSERT(bs2->num_words == 1, "Expected 1 word (64 bits), got %zu", bs2->num_words);
  SUITE_ASSERT(bs2->words[0] == 0, "bs2 words[0] not zero-initialized");


  Bitset *bs3 = bitset_create(0, arena);
  SUITE_ASSERT(bs3 != NULL, "bs3 returned NULL");
  SUITE_ASSERT(bs3->num_bits == 0, "Expected 0 bits, got %zu", bs3->num_bits);
  SUITE_ASSERT(bs3->num_words == 0, "Expected 0 words, got %zu", bs3->num_words);
  SUITE_ASSERT(bs3->words == NULL, "Expected NULL words for 0 bits, got %p", bs3->words);


  Bitset *bs_all = bitset_create_all(100, arena);
  SUITE_ASSERT(bs_all != NULL, "bitset_create_all returned NULL");
  SUITE_ASSERT(bs_all->num_bits == 100, "bs_all num_bits should be 100, got %zu", bs_all->num_bits);
  SUITE_ASSERT(bs_all->num_words == 2, "bs_all num_words should be 2, got %zu", bs_all->num_words);


  SUITE_ASSERT(bs_all->words[0] == 0xFFFFFFFFFFFFFFFF, "bs_all words[0] should be all 1s");




  uint64_t expected_mask = ((uint64_t)1 << 36) - 1;
  SUITE_ASSERT(bs_all->words[1] == expected_mask, "bs_all words[1] mask incorrect. Expected 0x%lx, got 0x%lx",
               (unsigned long)expected_mask, (unsigned long)bs_all->words[1]);

  SUITE_ASSERT(bitset_test(bs_all, 0) == true, "bs_all bit 0 should be true");
  SUITE_ASSERT(bitset_test(bs_all, 63) == true, "bs_all bit 63 should be true");
  SUITE_ASSERT(bitset_test(bs_all, 64) == true, "bs_all bit 64 should be true");
  SUITE_ASSERT(bitset_test(bs_all, 99) == true, "bs_all bit 99 should be true");


  SUITE_END();
}

int
test_set_clear_test(Bump *arena)
{
  SUITE_START("Bitset: set/clear/test");

  Bitset *bs = bitset_create(150, arena);
  SUITE_ASSERT(bs->num_words == 3, "Expected 3 words for 150 bits, got %zu", bs->num_words);


  bitset_set(bs, 0);
  bitset_set(bs, 63);
  bitset_set(bs, 64);
  bitset_set(bs, 127);
  bitset_set(bs, 128);
  bitset_set(bs, 149);


  SUITE_ASSERT(bitset_test(bs, 0) == true, "bit 0 should be true");
  SUITE_ASSERT(bitset_test(bs, 63) == true, "bit 63 should be true");
  SUITE_ASSERT(bitset_test(bs, 64) == true, "bit 64 should be true");
  SUITE_ASSERT(bitset_test(bs, 127) == true, "bit 127 should be true");
  SUITE_ASSERT(bitset_test(bs, 128) == true, "bit 128 should be true");
  SUITE_ASSERT(bitset_test(bs, 149) == true, "bit 149 should be true");


  SUITE_ASSERT(bitset_test(bs, 1) == false, "bit 1 should be false");
  SUITE_ASSERT(bitset_test(bs, 62) == false, "bit 62 should be false");
  SUITE_ASSERT(bitset_test(bs, 65) == false, "bit 65 should be false");
  SUITE_ASSERT(bitset_test(bs, 148) == false, "bit 148 should be false");


  uint64_t w0_expected = ((uint64_t)1 | ((uint64_t)1 << 63));
  uint64_t w1_expected = ((uint64_t)1 | ((uint64_t)1 << 63));
  uint64_t w2_expected = ((uint64_t)1 | ((uint64_t)1 << 21));
  SUITE_ASSERT(bs->words[0] == w0_expected, "words[0] raw value mismatch");
  SUITE_ASSERT(bs->words[1] == w1_expected, "words[1] raw value mismatch");
  SUITE_ASSERT(bs->words[2] == w2_expected, "words[2] raw value mismatch");


  bitset_clear(bs, 63);
  SUITE_ASSERT(bitset_test(bs, 63) == false, "bit 63 should be false after clear");
  SUITE_ASSERT(bs->words[0] == (uint64_t)1, "words[0] should only have bit 0 set");

  bitset_clear(bs, 64);
  SUITE_ASSERT(bitset_test(bs, 64) == false, "bit 64 should be false after clear");
  SUITE_ASSERT(bs->words[1] == ((uint64_t)1 << 63), "words[1] should only have bit 127 set");

  bitset_clear(bs, 149);
  SUITE_ASSERT(bitset_test(bs, 149) == false, "bit 149 should be false after clear");
  SUITE_ASSERT(bs->words[2] == (uint64_t)1, "words[2] should only have bit 128 set");

  SUITE_END();
}

int
test_all_clear(Bump *arena)
{
  SUITE_START("Bitset: set_all / clear_all");

  Bitset *bs = bitset_create(100, arena);


  bitset_set_all(bs);
  SUITE_ASSERT(bitset_test(bs, 0) == true, "bit 0 should be true after set_all");
  SUITE_ASSERT(bitset_test(bs, 99) == true, "bit 99 should be true after set_all");
  SUITE_ASSERT(bs->words[0] == 0xFFFFFFFFFFFFFFFF, "words[0] should be all 1s after set_all");
  uint64_t expected_mask = ((uint64_t)1 << 36) - 1;
  SUITE_ASSERT(bs->words[1] == expected_mask, "words[1] mask incorrect after set_all");


  bitset_clear_all(bs);
  SUITE_ASSERT(bitset_test(bs, 0) == false, "bit 0 should be false after clear_all");
  SUITE_ASSERT(bitset_test(bs, 99) == false, "bit 99 should be false after clear_all");
  SUITE_ASSERT(bs->words[0] == 0, "words[0] should be 0 after clear_all");
  SUITE_ASSERT(bs->words[1] == 0, "words[1] should be 0 after clear_all");

  SUITE_END();
}

int
test_copy_equals(Bump *arena)
{
  SUITE_START("Bitset: copy / equals");

  Bitset *bs1 = bitset_create(100, arena);
  Bitset *bs2 = bitset_create(100, arena);
  Bitset *bs3 = bitset_create(100, arena);

  bitset_set(bs1, 10);
  bitset_set(bs1, 50);
  bitset_set(bs1, 90);

  bitset_set(bs3, 10);
  bitset_set(bs3, 50);
  bitset_set(bs3, 90);

  SUITE_ASSERT(bitset_equals(bs1, bs2) == false, "bs1 should not equal empty bs2");
  SUITE_ASSERT(bitset_equals(bs1, bs3) == true, "bs1 should equal bs3");


  bitset_copy(bs2, bs1);
  SUITE_ASSERT(bitset_equals(bs1, bs2) == true, "bs2 should equal bs1 after copy");
  SUITE_ASSERT(bitset_test(bs2, 10) == true, "bs2 bit 10 incorrect after copy");
  SUITE_ASSERT(bitset_test(bs2, 50) == true, "bs2 bit 50 incorrect after copy");
  SUITE_ASSERT(bitset_test(bs2, 90) == true, "bs2 bit 90 incorrect after copy");
  SUITE_ASSERT(bitset_test(bs2, 1) == false, "bs2 bit 1 incorrect after copy");


  bitset_set(bs1, 1);
  SUITE_ASSERT(bitset_test(bs1, 1) == true, "bs1 bit 1 should be set");
  SUITE_ASSERT(bitset_test(bs2, 1) == false, "bs2 bit 1 should *not* be set (copy modified)");
  SUITE_ASSERT(bitset_equals(bs1, bs2) == false, "bs1 should no longer equal bs2");

  SUITE_END();
}

int
test_ops(Bump *arena)
{
  SUITE_START("Bitset: operations (intersect, union, diff)");

  size_t N = 100;
  Bitset *bs1 = bitset_create(N, arena);
  Bitset *bs2 = bitset_create(N, arena);
  Bitset *dest = bitset_create(N, arena);

  bitset_set(bs1, 1);
  bitset_set(bs1, 10);
  bitset_set(bs1, 50);

  bitset_set(bs2, 10);
  bitset_set(bs2, 50);
  bitset_set(bs2, 99);


  bitset_intersect(dest, bs1, bs2);
  SUITE_ASSERT(bitset_test(dest, 1) == false, "Intersect: bit 1 should be false");
  SUITE_ASSERT(bitset_test(dest, 10) == true, "Intersect: bit 10 should be true");
  SUITE_ASSERT(bitset_test(dest, 50) == true, "Intersect: bit 50 should be true");
  SUITE_ASSERT(bitset_test(dest, 99) == false, "Intersect: bit 99 should be false");


  bitset_union(dest, bs1, bs2);
  SUITE_ASSERT(bitset_test(dest, 1) == true, "Union: bit 1 should be true");
  SUITE_ASSERT(bitset_test(dest, 10) == true, "Union: bit 10 should be true");
  SUITE_ASSERT(bitset_test(dest, 50) == true, "Union: bit 50 should be true");
  SUITE_ASSERT(bitset_test(dest, 99) == true, "Union: bit 99 should be true");
  SUITE_ASSERT(bitset_test(dest, 2) == false, "Union: bit 2 should be false");


  bitset_difference(dest, bs1, bs2);
  SUITE_ASSERT(bitset_test(dest, 1) == true, "Difference (bs1-bs2): bit 1 should be true");
  SUITE_ASSERT(bitset_test(dest, 10) == false, "Difference (bs1-bs2): bit 10 should be false");
  SUITE_ASSERT(bitset_test(dest, 50) == false, "Difference (bs1-bs2): bit 50 should be false");
  SUITE_ASSERT(bitset_test(dest, 99) == false, "Difference (bs1-bs2): bit 99 should be false");

  bitset_difference(dest, bs2, bs1);
  SUITE_ASSERT(bitset_test(dest, 1) == false, "Difference (bs2-bs1): bit 1 should be false");
  SUITE_ASSERT(bitset_test(dest, 10) == false, "Difference (bs2-bs1): bit 10 should be false");
  SUITE_ASSERT(bitset_test(dest, 50) == false, "Difference (bs2-bs1): bit 50 should be false");
  SUITE_ASSERT(bitset_test(dest, 99) == true, "Difference (bs2-bs1): bit 99 should be true");

  SUITE_END();
}

int
test_count_slow(Bump *arena)
{
  SUITE_START("Bitset: count_slow");

  Bitset *bs1 = bitset_create(100, arena);
  SUITE_ASSERT(bitset_count_slow(bs1) == 0, "Empty bitset count should be 0");

  bitset_set(bs1, 1);
  bitset_set(bs1, 10);
  bitset_set(bs1, 99);
  SUITE_ASSERT(bitset_count_slow(bs1) == 3, "Count should be 3, got %zu", bitset_count_slow(bs1));

  bitset_set(bs1, 63);
  bitset_set(bs1, 64);
  SUITE_ASSERT(bitset_count_slow(bs1) == 5, "Count should be 5, got %zu", bitset_count_slow(bs1));

  bitset_clear(bs1, 10);
  SUITE_ASSERT(bitset_count_slow(bs1) == 4, "Count should be 4, got %zu", bitset_count_slow(bs1));


  Bitset *bs_all = bitset_create_all(100, arena);
  SUITE_ASSERT(bitset_count_slow(bs_all) == 100, "Count all (100) should be 100, got %zu", bitset_count_slow(bs_all));

  Bitset *bs_all_64 = bitset_create_all(64, arena);
  SUITE_ASSERT(bitset_count_slow(bs_all_64) == 64, "Count all (64) should be 64, got %zu",
               bitset_count_slow(bs_all_64));

  SUITE_END();
}



int
main()
{

  Bump arena;
  bump_init(&arena);





  __calir_current_suite_name = "Bitset";

  __calir_total_suites_run++;
  if (test_create(&arena) != 0)
  {
    __calir_total_suites_failed++;
  }

  __calir_total_suites_run++;
  if (test_set_clear_test(&arena) != 0)
  {
    __calir_total_suites_failed++;
  }

  __calir_total_suites_run++;
  if (test_all_clear(&arena) != 0)
  {
    __calir_total_suites_failed++;
  }

  __calir_total_suites_run++;
  if (test_copy_equals(&arena) != 0)
  {
    __calir_total_suites_failed++;
  }

  __calir_total_suites_run++;
  if (test_ops(&arena) != 0)
  {
    __calir_total_suites_failed++;
  }

  __calir_total_suites_run++;
  if (test_count_slow(&arena) != 0)
  {
    __calir_total_suites_failed++;
  }


  bump_destroy(&arena);



  TEST_SUMMARY();
}
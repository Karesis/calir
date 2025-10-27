// tests/test_bitset.c

#include "utils/bitset.h"
#include "utils/bump.h"
#include <assert.h>
#include <stdio.h>
#include <string.h> // for memset

// --- 测试辅助宏 ---
static int test_count = 0;
static int pass_count = 0;

#define TEST_START(name)                                                                                               \
  do                                                                                                                   \
  {                                                                                                                    \
    printf("\n--- Test: %s ---\n", name);                                                                              \
    test_count = 0;                                                                                                    \
    pass_count = 0;                                                                                                    \
  } while (0)

#define ASSERT(condition)                                                                                              \
  do                                                                                                                   \
  {                                                                                                                    \
    test_count++;                                                                                                      \
    if (condition)                                                                                                     \
    {                                                                                                                  \
      pass_count++;                                                                                                    \
    }                                                                                                                  \
    else                                                                                                               \
    {                                                                                                                  \
      fprintf(stderr, "    [FAIL] Assertion failed at line %d: %s\n", __LINE__, #condition);                           \
    }                                                                                                                  \
  } while (0)

#define TEST_SUMMARY()                                                                                                 \
  do                                                                                                                   \
  {                                                                                                                    \
    printf("--- Summary: %d/%d passed ---\n", pass_count, test_count);                                                 \
    if (pass_count != test_count)                                                                                      \
    {                                                                                                                  \
      return 1; /* 返回失败 */                                                                                         \
    }                                                                                                                  \
    return 0; /* 返回成功 */                                                                                           \
  } while (0)

// --- 测试函数 ---

int
test_create(Bump *arena)
{
  TEST_START("test_create");

  // 测试1: 创建一个 100 位的空位集
  Bitset *bs1 = bitset_create(100, arena);
  ASSERT(bs1 != NULL);
  ASSERT(bs1->num_bits == 100);
  ASSERT(bs1->num_words == 2); // 100 / 64 = 1...36 -> 2 words
  ASSERT(bs1->words != NULL);
  ASSERT(bs1->words[0] == 0); // 必须是零初始化的
  ASSERT(bs1->words[1] == 0);

  // 检查 bitset_test 是否全为 false
  ASSERT(bitset_test(bs1, 0) == false);
  ASSERT(bitset_test(bs1, 99) == false);

  // 测试2: 创建一个 64 位的空位集 (边界情况)
  Bitset *bs2 = bitset_create(64, arena);
  ASSERT(bs2->num_bits == 64);
  ASSERT(bs2->num_words == 1);
  ASSERT(bs2->words[0] == 0);

  // 测试3: 创建一个 0 位的空位集 (边界情况)
  Bitset *bs3 = bitset_create(0, arena);
  ASSERT(bs3->num_bits == 0);
  ASSERT(bs3->num_words == 0);
  ASSERT(bs3->words == NULL);

  // 测试4: 创建一个 100 位的全1位集
  Bitset *bs_all = bitset_create_all(100, arena);
  ASSERT(bs_all != NULL);
  ASSERT(bs_all->num_bits == 100);
  ASSERT(bs_all->num_words == 2);

  // 检查第一个词 (0-63)
  ASSERT(bs_all->words[0] == 0xFFFFFFFFFFFFFFFF);

  // 检查第二个词 (64-99)
  // 应该有 100 - 64 = 36 位
  // (1 << 36) - 1
  uint64_t expected_mask = ((uint64_t)1 << 36) - 1;
  ASSERT(bs_all->words[1] == expected_mask);

  ASSERT(bitset_test(bs_all, 0) == true);
  ASSERT(bitset_test(bs_all, 63) == true);
  ASSERT(bitset_test(bs_all, 64) == true);
  ASSERT(bitset_test(bs_all, 99) == true);

  TEST_SUMMARY();
}

int
test_set_clear_test(Bump *arena)
{
  TEST_START("test_set_clear_test");

  Bitset *bs = bitset_create(150, arena); // 150 bits -> 3 words

  // 测试 set
  bitset_set(bs, 0);   // 第 0 词, 第 0 位
  bitset_set(bs, 63);  // 第 0 词, 第 63 位
  bitset_set(bs, 64);  // 第 1 词, 第 0 位
  bitset_set(bs, 127); // 第 1 词, 第 63 位
  bitset_set(bs, 128); // 第 2 词, 第 0 位
  bitset_set(bs, 149); // 第 2 词, 第 21 位

  // 测试 test
  ASSERT(bitset_test(bs, 0) == true);
  ASSERT(bitset_test(bs, 63) == true);
  ASSERT(bitset_test(bs, 64) == true);
  ASSERT(bitset_test(bs, 127) == true);
  ASSERT(bitset_test(bs, 128) == true);
  ASSERT(bitset_test(bs, 149) == true);

  // 测试未设置的位
  ASSERT(bitset_test(bs, 1) == false);
  ASSERT(bitset_test(bs, 62) == false);
  ASSERT(bitset_test(bs, 65) == false);
  ASSERT(bitset_test(bs, 148) == false);

  // 检查原始 words
  ASSERT(bs->words[0] == ((uint64_t)1 | ((uint64_t)1 << 63)));
  ASSERT(bs->words[1] == ((uint64_t)1 | ((uint64_t)1 << 63)));
  ASSERT(bs->words[2] == ((uint64_t)1 | ((uint64_t)1 << 21)));

  // 测试 clear
  bitset_clear(bs, 63);
  ASSERT(bitset_test(bs, 63) == false);
  ASSERT(bs->words[0] == (uint64_t)1); // 检查是否只清除了第 63 位

  bitset_clear(bs, 64);
  ASSERT(bitset_test(bs, 64) == false);
  ASSERT(bs->words[1] == ((uint64_t)1 << 63));

  bitset_clear(bs, 149);
  ASSERT(bitset_test(bs, 149) == false);
  ASSERT(bs->words[2] == (uint64_t)1);

  TEST_SUMMARY();
}

int
test_all_clear(Bump *arena)
{
  TEST_START("test_all_clear");

  Bitset *bs = bitset_create(100, arena); // 2 words

  // 1. 测试 set_all
  bitset_set_all(bs);
  ASSERT(bitset_test(bs, 0) == true);
  ASSERT(bitset_test(bs, 99) == true);
  ASSERT(bs->words[0] == 0xFFFFFFFFFFFFFFFF);
  uint64_t expected_mask = ((uint64_t)1 << 36) - 1; // 100 - 64 = 36
  ASSERT(bs->words[1] == expected_mask);

  // 2. 测试 clear_all
  bitset_clear_all(bs);
  ASSERT(bitset_test(bs, 0) == false);
  ASSERT(bitset_test(bs, 99) == false);
  ASSERT(bs->words[0] == 0);
  ASSERT(bs->words[1] == 0);

  TEST_SUMMARY();
}

int
test_copy_equals(Bump *arena)
{
  TEST_START("test_copy_equals");

  Bitset *bs1 = bitset_create(100, arena);
  Bitset *bs2 = bitset_create(100, arena);
  Bitset *bs3 = bitset_create(100, arena);

  bitset_set(bs1, 10);
  bitset_set(bs1, 50);
  bitset_set(bs1, 90);

  bitset_set(bs3, 10);
  bitset_set(bs3, 50);
  bitset_set(bs3, 90);

  ASSERT(bitset_equals(bs1, bs2) == false);
  ASSERT(bitset_equals(bs1, bs3) == true);

  // 测试 copy
  bitset_copy(bs2, bs1);
  ASSERT(bitset_equals(bs1, bs2) == true);
  ASSERT(bitset_test(bs2, 10) == true);
  ASSERT(bitset_test(bs2, 50) == true);
  ASSERT(bitset_test(bs2, 90) == true);
  ASSERT(bitset_test(bs2, 1) == false);

  // 修改 bs1，不应影响 bs2
  bitset_set(bs1, 1);
  ASSERT(bitset_test(bs1, 1) == true);
  ASSERT(bitset_test(bs2, 1) == false);
  ASSERT(bitset_equals(bs1, bs2) == false);

  TEST_SUMMARY();
}

int
test_ops(Bump *arena)
{
  TEST_START("test_ops (intersect, union, diff)");

  size_t N = 100;
  Bitset *bs1 = bitset_create(N, arena); // {1, 10, 50}
  Bitset *bs2 = bitset_create(N, arena); // {10, 50, 99}
  Bitset *dest = bitset_create(N, arena);

  bitset_set(bs1, 1);
  bitset_set(bs1, 10);
  bitset_set(bs1, 50);

  bitset_set(bs2, 10);
  bitset_set(bs2, 50);
  bitset_set(bs2, 99);

  // 1. 测试 intersect (交集)
  bitset_intersect(dest, bs1, bs2); // dest = {10, 50}
  ASSERT(bitset_test(dest, 1) == false);
  ASSERT(bitset_test(dest, 10) == true);
  ASSERT(bitset_test(dest, 50) == true);
  ASSERT(bitset_test(dest, 99) == false);

  // 2. 测试 union (并集)
  bitset_union(dest, bs1, bs2); // dest = {1, 10, 50, 99}
  ASSERT(bitset_test(dest, 1) == true);
  ASSERT(bitset_test(dest, 10) == true);
  ASSERT(bitset_test(dest, 50) == true);
  ASSERT(bitset_test(dest, 99) == true);
  ASSERT(bitset_test(dest, 2) == false);

  // 3. 测试 difference (差集)
  bitset_difference(dest, bs1, bs2); // dest = bs1 \ bs2 = {1}
  ASSERT(bitset_test(dest, 1) == true);
  ASSERT(bitset_test(dest, 10) == false);
  ASSERT(bitset_test(dest, 50) == false);
  ASSERT(bitset_test(dest, 99) == false);

  bitset_difference(dest, bs2, bs1); // dest = bs2 \ bs1 = {99}
  ASSERT(bitset_test(dest, 1) == false);
  ASSERT(bitset_test(dest, 10) == false);
  ASSERT(bitset_test(dest, 50) == false);
  ASSERT(bitset_test(dest, 99) == true);

  TEST_SUMMARY();
}

int
test_count_slow(Bump *arena)
{
  TEST_START("test_count_slow");

  Bitset *bs1 = bitset_create(100, arena);
  ASSERT(bitset_count_slow(bs1) == 0);

  bitset_set(bs1, 1);
  bitset_set(bs1, 10);
  bitset_set(bs1, 99);
  ASSERT(bitset_count_slow(bs1) == 3);

  bitset_set(bs1, 63);
  bitset_set(bs1, 64);
  ASSERT(bitset_count_slow(bs1) == 5);

  bitset_clear(bs1, 10);
  ASSERT(bitset_count_slow(bs1) == 4);

  // 测试全集
  Bitset *bs_all = bitset_create_all(100, arena);
  ASSERT(bitset_count_slow(bs_all) == 100);

  Bitset *bs_all_64 = bitset_create_all(64, arena);
  ASSERT(bitset_count_slow(bs_all_64) == 64);

  TEST_SUMMARY();
}

// --- 主函数 ---

int
main()
{
  // 1. 初始化竞技场
  Bump arena;
  bump_init(&arena);

  int failed = 0;

  // 2. 运行所有测试套件
  failed |= test_create(&arena);
  failed |= test_set_clear_test(&arena);
  failed |= test_all_clear(&arena);
  failed |= test_copy_equals(&arena);
  failed |= test_ops(&arena);
  failed |= test_count_slow(&arena);

  // 3. 销毁竞技场
  bump_destroy(&arena);

  if (failed)
  {
    fprintf(stderr, "\n[!!!] Bitset tests FAILED.\n");
    return 1;
  }
  else
  {
    printf("\n[OK] All Bitset tests passed.\n");
    return 0;
  }
}
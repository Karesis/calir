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


#include <math.h> // for fabs
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
// [!!] 1. 移除 <assert.h>

// 包含 bump 分配器
#include "utils/bump.h"

// 包含所有 hashmap API
#include "utils/hashmap/float.h"
#include "utils/hashmap/generic.h"
#include "utils/hashmap/int.h"
#include "utils/hashmap/ptr.h"
#include "utils/hashmap/str_slice.h"

// 包含 xxhash.h (generic 测试需要)
#define XXH_INLINE_ALL
#include "utils/xxhash.h"

// [!!] 2. 包含我们统一的测试框架
#include "test_utils.h"

/*
 * ========================================
 * --- [!!] 3. 移除旧的测试框架 ---
 * ========================================
 */
// (移除了 tests_run, tests_failed, TEST_ASSERT, RUN_TEST)

/*
 * ========================================
 * --- I64HashMap (整数) 测试 ---
 * ========================================
 */
// [!!] 4. 函数签名改为返回 int
int
test_int_iterators()
{
  // [!!] 5. 使用 SUITE_START
  SUITE_START("HashMap Iter: I64");

  Bump *arena = bump_new();
  I64HashMap *map = i64_hashmap_create(arena, 0);

  // 1. 测试空迭代
  printf("  Testing empty map...\n");
  int count = 0;
  I64HashMapIter iter = i64_hashmap_iter(map);
  I64HashMapEntry entry;
  while (i64_hashmap_iter_next(&iter, &entry))
  {
    count++;
  }
  // [!!] 6. 使用 SUITE_ASSERT
  SUITE_ASSERT(count == 0, "Empty map iterator yielded %d items", count);

  // 2. 填充 map, 触发 grow
  printf("  Testing full map (with grow)...\n");
  int64_t key_sum = 0;
  intptr_t val_sum = 0;
  for (int64_t i = 1; i <= 100; i++)
  {
    i64_hashmap_put(map, i, (void *)(uintptr_t)i);
  }
  SUITE_ASSERT(i64_hashmap_size(map) == 100, "Map size should be 100, got %zu", i64_hashmap_size(map));

  count = 0;
  iter = i64_hashmap_iter(map);
  while (i64_hashmap_iter_next(&iter, &entry))
  {
    count++;
    key_sum += entry.key;
    val_sum += (intptr_t)entry.value;
  }
  SUITE_ASSERT(count == 100, "Full map iterator yielded %d items (expected 100)", count);
  SUITE_ASSERT(key_sum == 5050, "Sum of keys should be 5050, got %lld", (long long)key_sum);
  SUITE_ASSERT(val_sum == 5050, "Sum of values should be 5050, got %ld", (long)val_sum);

  // 3. (关键) 测试带墓碑 (Tombstones) 的迭代
  printf("  Testing map with tombstones...\n");
  // 移除所有偶数
  for (int64_t i = 2; i <= 100; i += 2)
  {
    i64_hashmap_remove(map, i);
  }
  SUITE_ASSERT(i64_hashmap_size(map) == 50, "Map size after removes should be 50, got %zu", i64_hashmap_size(map));

  count = 0;
  key_sum = 0;
  val_sum = 0;
  iter = i64_hashmap_iter(map);
  while (i64_hashmap_iter_next(&iter, &entry))
  {
    count++;
    key_sum += entry.key;
    val_sum += (intptr_t)entry.value;
    SUITE_ASSERT(entry.key % 2 != 0, "Iterator returned an even key (%lld), should have been skipped",
                 (long long)entry.key);
  }
  SUITE_ASSERT(count == 50, "Tombstone map iterator yielded %d items (expected 50)", count);
  int64_t expected_key_sum = 2500; // 5050 - 2550
  intptr_t expected_val_sum = 2500;
  SUITE_ASSERT(key_sum == expected_key_sum, "Sum of keys should be %lld, got %lld", (long long)expected_key_sum,
               (long long)key_sum);
  SUITE_ASSERT(val_sum == expected_val_sum, "Sum of values should be %ld, got %ld", (long)expected_val_sum,
               (long)val_sum);

  // [!!] 7. 在 SUITE_END 之前释放 arena
  bump_free(arena);
  // [!!] 8. 使用 SUITE_END
  SUITE_END();
}

/*
 * ========================================
 * --- F64HashMap (浮点数) 测试 ---
 * ========================================
 */
int
test_float_iterators()
{
  SUITE_START("HashMap Iter: F64");

  Bump *arena = bump_new();
  F64HashMap *map = f64_hashmap_create(arena, 0);

  // 1. 填充并移除
  printf("  Testing float map with tombstones...\n");
  f64_hashmap_put(map, 1.1, (void *)1);
  f64_hashmap_put(map, 2.2, (void *)2);
  f64_hashmap_put(map, 3.3, (void *)3);
  f64_hashmap_put(map, 4.4, (void *)4);

  f64_hashmap_remove(map, 2.2);
  f64_hashmap_remove(map, 4.4);
  SUITE_ASSERT(f64_hashmap_size(map) == 2, "Float map size should be 2, got %zu", f64_hashmap_size(map));

  // 2. 迭代
  int count = 0;
  double key_sum = 0;
  F64HashMapIter iter = f64_hashmap_iter(map);
  F64HashMapEntry entry;
  while (f64_hashmap_iter_next(&iter, &entry))
  {
    count++;
    key_sum += entry.key;
  }
  SUITE_ASSERT(count == 2, "Float map iterator yielded %d items (expected 2)", count);
  SUITE_ASSERT(fabs(key_sum - (1.1 + 3.3)) < 1e-9, "Sum of keys should be 4.4, got %f", key_sum);

  bump_free(arena);
  SUITE_END();
}

/*
 * ========================================
 * --- PtrHashMap (指针) 测试 ---
 * ========================================
 */
int
test_ptr_iterators()
{
  SUITE_START("HashMap Iter: Ptr");

  Bump *arena = bump_new();
  PtrHashMap *map = ptr_hashmap_create(arena, 0);

  int k1, k2, k3, k4;

  printf("  Testing ptr map with tombstones...\n");
  ptr_hashmap_put(map, &k1, (void *)1);
  ptr_hashmap_put(map, &k2, (void *)2);
  ptr_hashmap_put(map, &k3, (void *)3);
  ptr_hashmap_put(map, &k4, (void *)4);

  ptr_hashmap_remove(map, &k2);
  ptr_hashmap_remove(map, &k4);
  SUITE_ASSERT(ptr_hashmap_size(map) == 2, "Ptr map size should be 2, got %zu", ptr_hashmap_size(map));

  int count = 0;
  bool found_k1 = false;
  bool found_k3 = false;
  PtrHashMapIter iter = ptr_hashmap_iter(map);
  PtrHashMapEntry entry;
  while (ptr_hashmap_iter_next(&iter, &entry))
  {
    count++;
    if (entry.key == &k1)
      found_k1 = true;
    if (entry.key == &k3)
      found_k3 = true;
  }

  SUITE_ASSERT(count == 2, "Ptr map iterator yielded %d items (expected 2)", count);
  SUITE_ASSERT(found_k1, "Ptr map iterator did not find k1");
  SUITE_ASSERT(found_k3, "Ptr map iterator did not find k3");

  bump_free(arena);
  SUITE_END();
}

/*
 * ========================================
 * --- StrHashMap (字符串) 测试 ---
 * ========================================
 */
int
test_str_iterators()
{
  SUITE_START("HashMap Iter: Str");

  Bump *arena = bump_new();
  StrHashMap *map = str_hashmap_create(arena, 0);

  printf("  Testing str map with tombstones...\n");
  str_hashmap_put(map, "apple", 5, (void *)1);
  str_hashmap_put(map, "banana", 6, (void *)2);
  str_hashmap_put(map, "carrot", 6, (void *)3);
  str_hashmap_put(map, "date", 4, (void *)4);

  str_hashmap_remove(map, "banana", 6);
  str_hashmap_remove(map, "date", 4);
  SUITE_ASSERT(str_hashmap_size(map) == 2, "Str map size should be 2, got %zu", str_hashmap_size(map));

  int count = 0;
  bool found_apple = false;
  bool found_carrot = false;

  StrHashMapIter iter = str_hashmap_iter(map);
  StrHashMapEntry entry;
  while (str_hashmap_iter_next(&iter, &entry))
  {
    count++;
    if (entry.key_len == 5 && strncmp(entry.key_body, "apple", 5) == 0)
    {
      found_apple = true;
    }
    if (entry.key_len == 6 && strncmp(entry.key_body, "carrot", 6) == 0)
    {
      found_carrot = true;
    }
  }

  SUITE_ASSERT(count == 2, "Str map iterator yielded %d items (expected 2)", count);
  SUITE_ASSERT(found_apple, "Str map iterator did not find 'apple'");
  SUITE_ASSERT(found_carrot, "Str map iterator did not find 'carrot'");

  bump_free(arena);
  SUITE_END();
}

/*
 * ========================================
 * --- GenericHashMap (泛型) 测试 ---
 * ========================================
 */

// (辅助函数保持不变)
static uint64_t
generic_hash_fn(const void *key)
{
  return XXH3_64bits(key, sizeof(int64_t));
}

static bool
generic_equal_fn(const void *key1, const void *key2)
{
  return *(const int64_t *)key1 == *(const int64_t *)key2;
}

int
test_generic_iterators()
{
  SUITE_START("HashMap Iter: Generic");

  Bump *arena = bump_new();
  GenericHashMap *map = generic_hashmap_create(arena, 0, generic_hash_fn, generic_equal_fn);

  int64_t k1 = 101, k2 = 202, k3 = 303, k4 = 404;

  printf("  Testing generic map with tombstones...\n");
  generic_hashmap_put(map, &k1, (void *)1);
  generic_hashmap_put(map, &k2, (void *)2);
  generic_hashmap_put(map, &k3, (void *)3);
  generic_hashmap_put(map, &k4, (void *)4);

  generic_hashmap_remove(map, &k1);
  generic_hashmap_remove(map, &k3);
  SUITE_ASSERT(generic_hashmap_size(map) == 2, "Generic map size should be 2, got %zu", generic_hashmap_size(map));

  int count = 0;
  bool found_k2 = false;
  bool found_k4 = false;

  GenericHashMapIter iter = generic_hashmap_iter(map);
  GenericHashMapEntry entry;
  while (generic_hashmap_iter_next(&iter, &entry))
  {
    count++;
    const int64_t *key_ptr = (const int64_t *)entry.key;
    if (*key_ptr == k2)
      found_k2 = true;
    if (*key_ptr == k4)
      found_k4 = true;
  }

  SUITE_ASSERT(count == 2, "Generic map iterator yielded %d items (expected 2)", count);
  SUITE_ASSERT(found_k2, "Generic map iterator did not find k2 (202)");
  SUITE_ASSERT(found_k4, "Generic map iterator did not find k4 (404)");

  bump_free(arena);
  SUITE_END();
}

/*
 * ========================================
 * --- Main ---
 * ========================================
 */

// [!!] 9. 使用我们新的、统一的 main 运行器
int
main()
{
  // 设置此测试文件的总套件名称
  __calir_current_suite_name = "HashMap Iterators";

  __calir_total_suites_run++;
  if (test_int_iterators() != 0)
  {
    __calir_total_suites_failed++;
  }

  __calir_total_suites_run++;
  if (test_float_iterators() != 0)
  {
    __calir_total_suites_failed++;
  }

  __calir_total_suites_run++;
  if (test_ptr_iterators() != 0)
  {
    __calir_total_suites_failed++;
  }

  __calir_total_suites_run++;
  if (test_str_iterators() != 0)
  {
    __calir_total_suites_failed++;
  }

  __calir_total_suites_run++;
  if (test_generic_iterators() != 0)
  {
    __calir_total_suites_failed++;
  }

  // 打印最终摘要并返回 0 或 1
  TEST_SUMMARY();
}
/*
 * tests/test_hashmap.c
 *
 * 综合测试套件, 用于验证 hashmap 的 'states' 数组重构
 * 和新的 'GenericHashMap' (structs) 实现。
 */

// [!!] 1. 移除 <assert.h> 和 <stdlib.h>
#include <limits.h> // 用于 INT_MAX 等
#include <math.h>   // 用于 INFINITY, nan()
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// 包含所有 hashmap 的实现
#include "utils/bump.h"
#include "utils/hashmap.h"

// [!!] 2. 包含我们统一的测试框架
#include "test_utils.h"

// [!!] 3. 移除旧的测试框架 (total_tests, total_fails, TEST_ASSERT, RUN_TEST)

// 全局 Arena, 用于所有测试 (保持不变)
static Bump global_arena;

// 2. --- 测试实现 ---

/**
 * @brief 测试 I64HashMap 和 U64HashMap
 */
// [!!] 4. 签名改为返回 int
int
test_int_hashmap()
{
  // [!!] 5. 使用 SUITE_START
  SUITE_START("HashMap Core: Integers (i64, u64, i32)");

  // --- I64HashMap ---
  I64HashMap *i_map = i64_hashmap_create(&global_arena, 8);
  // [!!] 6. 使用 SUITE_ASSERT
  SUITE_ASSERT(i_map != NULL, "i64_hashmap_create failed");
  SUITE_ASSERT(i64_hashmap_size(i_map) == 0, "i64 map initial size not 0");

  int v1 = 100, v2 = 200, v3 = 300;

  // 基本 put/get
  i64_hashmap_put(i_map, 42, &v1);
  SUITE_ASSERT(i64_hashmap_size(i_map) == 1, "i64 map size should be 1 after put");
  SUITE_ASSERT(i64_hashmap_get(i_map, 42) == &v1, "i64 map get failed");
  SUITE_ASSERT(i64_hashmap_contains(i_map, 42) == true, "i64 map contains failed");

  // Overwrite
  i64_hashmap_put(i_map, 42, &v2);
  SUITE_ASSERT(i64_hashmap_size(i_map) == 1, "i64 map size should not change on overwrite");
  SUITE_ASSERT(i64_hashmap_get(i_map, 42) == &v2, "i64 map get failed after overwrite");

  // [关键] 测试旧的哨兵值
  i64_hashmap_put(i_map, INT64_MAX, &v3);     // 曾是 i_map 的 EMPTY
  i64_hashmap_put(i_map, INT64_MAX - 1, &v1); // 曾是 i_map 的 TOMBSTONE

  SUITE_ASSERT(i64_hashmap_size(i_map) == 3, "i64 map size should be 3 after sentinel puts");
  SUITE_ASSERT(i64_hashmap_get(i_map, INT64_MAX) == &v3, "i64 map failed to get INT64_MAX");
  SUITE_ASSERT(i64_hashmap_get(i_map, INT64_MAX - 1) == &v1, "i64 map failed to get INT64_MAX-1");

  // --- U64HashMap ---
  U64HashMap *u_map = u64_hashmap_create(&global_arena, 8);
  SUITE_ASSERT(u_map != NULL, "u64_hashmap_create failed");

  // [关键] 测试旧的哨兵值
  u64_hashmap_put(u_map, 0, &v1);            // 曾是 u_map 的 EMPTY
  u64_hashmap_put(u_map, (uint64_t)-1, &v2); // 曾是 u_map 的 TOMBSTONE (UINT64_MAX)

  SUITE_ASSERT(u64_hashmap_size(u_map) == 2, "u64 map size should be 2 after sentinel puts");
  SUITE_ASSERT(u64_hashmap_get(u_map, 0) == &v1, "u64 map failed to get 0");
  SUITE_ASSERT(u64_hashmap_get(u_map, (uint64_t)-1) == &v2, "u64 map failed to get UINT64_MAX");

  // 测试 remove
  bool removed = u64_hashmap_remove(u_map, 0);
  SUITE_ASSERT(removed == true, "u64 map remove(0) should return true");
  SUITE_ASSERT(u64_hashmap_size(u_map) == 1, "u64 map size should be 1 after remove");
  SUITE_ASSERT(u64_hashmap_get(u_map, 0) == NULL, "u64 map get(0) should return NULL after remove");
  SUITE_ASSERT(u64_hashmap_contains(u_map, 0) == false, "u64 map contains(0) should be false after remove");
  SUITE_ASSERT(u64_hashmap_contains(u_map, (uint64_t)-1) == true, "u64 map should still contain UINT64_MAX");

  // 测试 remove 不存在的 key
  removed = u64_hashmap_remove(u_map, 123);
  SUITE_ASSERT(removed == false, "u64 map remove(123) should return false");
  SUITE_ASSERT(u64_hashmap_size(u_map) == 1, "u64 map size should be unchanged after failed remove");

  // --- 测试 Grow (Resize) ---
  I32HashMap *grow_map = i32_hashmap_create(&global_arena, 2); // 故意设置小容量
  int values[100];
  for (int i = 0; i < 100; i++)
  {
    values[i] = i * 10;
    i32_hashmap_put(grow_map, i, &values[i]);
  }
  SUITE_ASSERT(i32_hashmap_size(grow_map) == 100, "i32 map size should be 100 after grow");
  // 验证数据在 grow 后仍然存在
  SUITE_ASSERT(i32_hashmap_get(grow_map, 50) != NULL, "i32 map get(50) should not be NULL");
  SUITE_ASSERT(*(int *)i32_hashmap_get(grow_map, 50) == 500, "i32 map get(50) value incorrect after grow");
  SUITE_ASSERT(i32_hashmap_get(grow_map, 99) != NULL, "i32 map get(99) should not be NULL");
  SUITE_ASSERT(*(int *)i32_hashmap_get(grow_map, 99) == 990, "i32 map get(99) value incorrect after grow");
  SUITE_ASSERT(i32_hashmap_get(grow_map, 101) == NULL, "i32 map get(101) should be NULL");

  // [!!] 7. 使用 SUITE_END
  SUITE_END();
}

/**
 * @brief 测试 PtrHashMap
 */
int
test_ptr_hashmap()
{
  SUITE_START("HashMap Core: Ptr");

  PtrHashMap *map = ptr_hashmap_create(&global_arena, 8);
  SUITE_ASSERT(map != NULL, "ptr_hashmap_create failed");

  int v1 = 1, v2 = 2, v3 = 3, v4 = 4;
  void *k1 = &v1;
  void *k2 = &v2;

  ptr_hashmap_put(map, k1, &v1);
  ptr_hashmap_put(map, k2, &v2);
  SUITE_ASSERT(ptr_hashmap_size(map) == 2, "ptr map size should be 2");
  SUITE_ASSERT(ptr_hashmap_get(map, k1) == &v1, "ptr map get(k1) failed");

  // [关键] 测试旧的哨兵值
  ptr_hashmap_put(map, NULL, &v3);       // 曾是 EMPTY
  ptr_hashmap_put(map, (void *)-1, &v4); // 曾是 TOMBSTONE

  SUITE_ASSERT(ptr_hashmap_size(map) == 4, "ptr map size should be 4 after sentinel puts");
  SUITE_ASSERT(ptr_hashmap_get(map, NULL) == &v3, "ptr map failed to get NULL key");
  SUITE_ASSERT(ptr_hashmap_get(map, (void *)-1) == &v4, "ptr map failed to get (void*)-1 key");

  // Remove
  ptr_hashmap_remove(map, k1);
  SUITE_ASSERT(ptr_hashmap_size(map) == 3, "ptr map size should be 3 after remove");
  SUITE_ASSERT(ptr_hashmap_get(map, k1) == NULL, "ptr map get(k1) should be NULL after remove");
  SUITE_ASSERT(ptr_hashmap_contains(map, NULL) == true, "ptr map should still contain NULL key");

  SUITE_END();
}

/**
 * @brief 测试 StrHashMap
 */
int
test_str_hashmap()
{
  SUITE_START("HashMap Core: Str");

  StrHashMap *map = str_hashmap_create(&global_arena, 8);
  SUITE_ASSERT(map != NULL, "str_hashmap_create failed");

  int v1 = 1, v2 = 2, v3 = 3;
  str_hashmap_put(map, "hello", 5, &v1);
  str_hashmap_put(map, "world", 5, &v2);
  SUITE_ASSERT(str_hashmap_size(map) == 2, "str map size should be 2");
  SUITE_ASSERT(str_hashmap_get(map, "hello", 5) == &v1, "str map get('hello') failed");

  // Test get with different pointer, same content
  char key_copy[] = "hello";
  SUITE_ASSERT(str_hashmap_get(map, key_copy, 5) == &v1, "str map value-based lookup failed");

  // Test empty string
  str_hashmap_put(map, "", 0, &v3);
  SUITE_ASSERT(str_hashmap_size(map) == 3, "str map size should be 3 after putting empty string");
  SUITE_ASSERT(str_hashmap_get(map, "", 0) == &v3, "str map get empty string failed");

  // Test remove
  str_hashmap_remove(map, "hello", 5);
  SUITE_ASSERT(str_hashmap_size(map) == 2, "str map size should be 2 after remove");
  SUITE_ASSERT(str_hashmap_get(map, "hello", 5) == NULL, "str map get('hello') should be NULL after remove");
  SUITE_ASSERT(str_hashmap_contains(map, "world", 5) == true, "str map should still contain 'world'");

  SUITE_END();
}

/**
 * @brief 测试 F64HashMap
 */
int
test_float_hashmap()
{
  SUITE_START("HashMap Core: F64");

  F64HashMap *map = f64_hashmap_create(&global_arena, 8);
  SUITE_ASSERT(map != NULL, "f64_hashmap_create failed");

  int v1 = 1, v_inf = 100, v_ninf = 200, v_zero = 0;

  f64_hashmap_put(map, 123.456, &v1);
  SUITE_ASSERT(f64_hashmap_size(map) == 1, "f64 map size should be 1");

  // 0.0 和 -0.0
  f64_hashmap_put(map, 0.0, &v_zero);
  SUITE_ASSERT(f64_hashmap_size(map) == 2, "f64 map size should be 2 after put(0.0)");
  f64_hashmap_put(map, -0.0, &v1); // k1 == k2 认为 0.0 == -0.0
  SUITE_ASSERT(f64_hashmap_size(map) == 2, "f64 map size should be 2 after put(-0.0) (overwrite)");
  SUITE_ASSERT(f64_hashmap_get(map, 0.0) == &v1, "f64 map get(0.0) failed after overwrite");
  SUITE_ASSERT(f64_hashmap_get(map, -0.0) == &v1, "f64 map get(-0.0) failed after overwrite");

  // [关键] 测试旧的哨兵值 (Inf)
  f64_hashmap_put(map, INFINITY, &v_inf);
  f64_hashmap_put(map, -INFINITY, &v_ninf);

  SUITE_ASSERT(f64_hashmap_size(map) == 4, "f64 map size should be 4 after Inf puts");
  SUITE_ASSERT(f64_hashmap_get(map, INFINITY) == &v_inf, "f64 map failed to get INFINITY");
  SUITE_ASSERT(f64_hashmap_get(map, -INFINITY) == &v_ninf, "f64 map failed to get -INFINITY");

  // [关键] 测试 NaN (NaN != NaN)
  SUITE_ASSERT(f64_hashmap_get(map, nan("")) == NULL, "f64 map get(NaN) should return NULL");
  SUITE_ASSERT(f64_hashmap_contains(map, nan("")) == false, "f64 map contains(NaN) should be false");

  SUITE_END();
}

/**
 * @brief 测试 GenericHashMap (用于 Structs)
 */

// (辅助结构和函数保持不变)
typedef struct
{
  int id;
  const char *tag;
} MyStructKey;

uint64_t
my_struct_hash(const void *key)
{
  const MyStructKey *k = (const MyStructKey *)key;
  uint64_t tag_hash = 0;
  if (k->tag)
  {
    tag_hash = 14695981039346656037ULL;
    for (const char *p = k->tag; *p; p++)
    {
      tag_hash ^= (uint64_t)(*p);
      tag_hash *= 1099511628211ULL;
    }
  }
  return (uint64_t)k->id ^ tag_hash;
}

bool
my_struct_equal(const void *key1, const void *key2)
{
  const MyStructKey *k1 = (const MyStructKey *)key1;
  const MyStructKey *k2 = (const MyStructKey *)key2;
  if (k1->id != k2->id)
    return false;
  if (k1->tag == k2->tag)
    return true;
  if (k1->tag == NULL || k2->tag == NULL)
    return false;
  return (strcmp(k1->tag, k2->tag) == 0);
}

int
test_generic_hashmap()
{
  SUITE_START("HashMap Core: Generic (Structs)");

  GenericHashMap *map = generic_hashmap_create(&global_arena, 8, my_struct_hash, my_struct_equal);
  SUITE_ASSERT(map != NULL, "generic_hashmap_create failed");

  int v1 = 10, v2 = 20;

  // Key 必须存储在稳定内存中 (Arena)
  MyStructKey *k1 = BUMP_ALLOC(&global_arena, MyStructKey);
  k1->id = 1;
  k1->tag = "TypeA";

  MyStructKey *k2 = BUMP_ALLOC(&global_arena, MyStructKey);
  k2->id = 2;
  k2->tag = "TypeB";

  generic_hashmap_put(map, k1, &v1);
  generic_hashmap_put(map, k2, &v2);
  SUITE_ASSERT(generic_hashmap_size(map) == 2, "generic map size should be 2");
  SUITE_ASSERT(generic_hashmap_get(map, k1) == &v1, "generic map get(k1) failed");

  // [关键] 测试值相等, 但指针不同
  MyStructKey stack_key;
  stack_key.id = 1;
  stack_key.tag = "TypeA";

  SUITE_ASSERT(generic_hashmap_get(map, &stack_key) == &v1, "generic map value-based get failed");
  SUITE_ASSERT(generic_hashmap_contains(map, &stack_key) == true, "generic map value-based contains failed");

  // 测试 Overwrite (使用 stack_key)
  generic_hashmap_put(map, &stack_key, &v2);
  SUITE_ASSERT(generic_hashmap_size(map) == 2, "generic map size should be 2 after overwrite");
  SUITE_ASSERT(generic_hashmap_get(map, k1) == &v2, "generic map get(k1) failed after overwrite");

  // 测试 Remove (使用 stack_key)
  stack_key.id = 2; // 指向 k2
  stack_key.tag = "TypeB";
  bool removed = generic_hashmap_remove(map, &stack_key);
  SUITE_ASSERT(removed == true, "generic map value-based remove failed");
  SUITE_ASSERT(generic_hashmap_size(map) == 1, "generic map size should be 1 after remove");
  SUITE_ASSERT(generic_hashmap_get(map, k2) == NULL, "generic map get(k2) should be NULL after remove");
  SUITE_ASSERT(generic_hashmap_get(map, k1) == &v2, "generic map k1 should still exist");

  SUITE_END();
}

// 3. --- Main 函数 ---

// [!!] 8. 重构 main 函数以使用新的运行器
int
main(void)
{
  // 初始化一个全局 Arena 供所有测试使用
  bump_init(&global_arena);

  printf("=== Calir HashMap Core Test Suite ===\n");

  // 设置此测试文件的总套件名称
  __calir_current_suite_name = "HashMap Core";

  __calir_total_suites_run++;
  if (test_int_hashmap() != 0)
  {
    __calir_total_suites_failed++;
  }

  __calir_total_suites_run++;
  if (test_ptr_hashmap() != 0)
  {
    __calir_total_suites_failed++;
  }

  __calir_total_suites_run++;
  if (test_str_hashmap() != 0)
  {
    __calir_total_suites_failed++;
  }

  __calir_total_suites_run++;
  if (test_float_hashmap() != 0)
  {
    __calir_total_suites_failed++;
  }

  __calir_total_suites_run++;
  if (test_generic_hashmap() != 0)
  {
    __calir_total_suites_failed++;
  }

  // 销毁 Arena, 释放其分配的所有内存块
  bump_destroy(&global_arena);

  // 打印最终摘要并返回 0 或 1
  TEST_SUMMARY();
}
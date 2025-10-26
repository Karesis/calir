/*
 * tests/test_hashmap.c
 *
 * 综合测试套件, 用于验证 hashmap 的 'states' 数组重构
 * 和新的 'GenericHashMap' (structs) 实现。
 */

#include <assert.h>
#include <limits.h> // 用于 INT_MAX 等
#include <math.h>   // 用于 INFINITY, nan()
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// 包含所有 hashmap 的实现
#include "utils/bump.h"
#include "utils/hashmap/float.h"
#include "utils/hashmap/generic.h"
#include "utils/hashmap/int.h"
#include "utils/hashmap/ptr.h"
#include "utils/hashmap/str_slice.h"

// 1. --- 简单的测试框架 ---

static int total_tests = 0;
static int total_fails = 0;

/**
 * @brief 断言一个条件。
 * 如果失败, 打印消息并增加失败计数。
 */
#define TEST_ASSERT(cond)                                                                                              \
  do                                                                                                                   \
  {                                                                                                                    \
    total_tests++;                                                                                                     \
    if (!(cond))                                                                                                       \
    {                                                                                                                  \
      total_fails++;                                                                                                   \
      printf("  [FAIL] %s:%d: Assertion failed: %s\n", __func__, __LINE__, #cond);                                     \
    }                                                                                                                  \
  } while (0)

/**
 * @brief 运行一个测试函数并打印其名称。
 */
#define RUN_TEST(test_func)                                                                                            \
  do                                                                                                                   \
  {                                                                                                                    \
    printf("\n--- Running %s ---\n", #test_func);                                                                      \
    test_func();                                                                                                       \
  } while (0)

// 全局 Arena, 用于所有测试
static Bump global_arena;

// 2. --- 测试实现 ---

/**
 * @brief 测试 I64HashMap 和 U64HashMap
 *
 * 关键测试: 验证旧的哨兵值 (INT_MAX, 0, -1) 现在是合法的 key。
 */
void
test_int_hashmap()
{
  // --- I64HashMap ---
  I64HashMap *i_map = i64_hashmap_create(&global_arena, 8);
  TEST_ASSERT(i_map != NULL);
  TEST_ASSERT(i64_hashmap_size(i_map) == 0);

  int v1 = 100, v2 = 200, v3 = 300;

  // 基本 put/get
  i64_hashmap_put(i_map, 42, &v1);
  TEST_ASSERT(i64_hashmap_size(i_map) == 1);
  TEST_ASSERT(i64_hashmap_get(i_map, 42) == &v1);
  TEST_ASSERT(i64_hashmap_contains(i_map, 42) == true);

  // Overwrite
  i64_hashmap_put(i_map, 42, &v2);
  TEST_ASSERT(i64_hashmap_size(i_map) == 1); // Size 不变
  TEST_ASSERT(i64_hashmap_get(i_map, 42) == &v2);

  // [关键] 测试旧的哨兵值
  i64_hashmap_put(i_map, INT64_MAX, &v3);     // 曾是 i_map 的 EMPTY
  i64_hashmap_put(i_map, INT64_MAX - 1, &v1); // 曾是 i_map 的 TOMBSTONE

  TEST_ASSERT(i64_hashmap_size(i_map) == 3);
  TEST_ASSERT(i64_hashmap_get(i_map, INT64_MAX) == &v3);
  TEST_ASSERT(i64_hashmap_get(i_map, INT64_MAX - 1) == &v1);
  printf("  [INFO] I64HashMap: Stored INT64_MAX successfully.\n");

  // --- U64HashMap ---
  U64HashMap *u_map = u64_hashmap_create(&global_arena, 8);

  // [关键] 测试旧的哨兵值
  u64_hashmap_put(u_map, 0, &v1);            // 曾是 u_map 的 EMPTY
  u64_hashmap_put(u_map, (uint64_t)-1, &v2); // 曾是 u_map 的 TOMBSTONE (UINT64_MAX)

  TEST_ASSERT(u64_hashmap_size(u_map) == 2);
  TEST_ASSERT(u64_hashmap_get(u_map, 0) == &v1);
  TEST_ASSERT(u64_hashmap_get(u_map, (uint64_t)-1) == &v2);
  printf("  [INFO] U64HashMap: Stored 0 and UINT64_MAX successfully.\n");

  // 测试 remove
  bool removed = u64_hashmap_remove(u_map, 0);
  TEST_ASSERT(removed == true);
  TEST_ASSERT(u64_hashmap_size(u_map) == 1);
  TEST_ASSERT(u64_hashmap_get(u_map, 0) == NULL);
  TEST_ASSERT(u64_hashmap_contains(u_map, 0) == false);
  TEST_ASSERT(u64_hashmap_contains(u_map, (uint64_t)-1) == true);

  // 测试 remove 不存在的 key
  removed = u64_hashmap_remove(u_map, 123);
  TEST_ASSERT(removed == false);
  TEST_ASSERT(u64_hashmap_size(u_map) == 1);

  // --- 测试 Grow (Resize) ---
  I32HashMap *grow_map = i32_hashmap_create(&global_arena, 2); // 故意设置小容量
  int values[100];
  for (int i = 0; i < 100; i++)
  {
    values[i] = i * 10;
    i32_hashmap_put(grow_map, i, &values[i]);
  }
  TEST_ASSERT(i32_hashmap_size(grow_map) == 100);
  // 验证数据在 grow 后仍然存在
  TEST_ASSERT(*(int *)i32_hashmap_get(grow_map, 50) == 500);
  TEST_ASSERT(*(int *)i32_hashmap_get(grow_map, 99) == 990);
  TEST_ASSERT(i32_hashmap_get(grow_map, 101) == NULL);
  printf("  [INFO] I32HashMap: Grow test passed (100 items).\n");
}

/**
 * @brief 测试 PtrHashMap
 *
 * 关键测试: 验证旧的哨兵值 (NULL 和 (void*)-1) 现在是合法的 key。
 */
void
test_ptr_hashmap()
{
  PtrHashMap *map = ptr_hashmap_create(&global_arena, 8);
  TEST_ASSERT(map != NULL);

  int v1 = 1, v2 = 2, v3 = 3, v4 = 4;
  void *k1 = &v1;
  void *k2 = &v2;

  ptr_hashmap_put(map, k1, &v1);
  ptr_hashmap_put(map, k2, &v2);
  TEST_ASSERT(ptr_hashmap_size(map) == 2);
  TEST_ASSERT(ptr_hashmap_get(map, k1) == &v1);

  // [关键] 测试旧的哨兵值
  ptr_hashmap_put(map, NULL, &v3);       // 曾是 EMPTY
  ptr_hashmap_put(map, (void *)-1, &v4); // 曾是 TOMBSTONE

  TEST_ASSERT(ptr_hashmap_size(map) == 4);
  TEST_ASSERT(ptr_hashmap_get(map, NULL) == &v3);
  TEST_ASSERT(ptr_hashmap_get(map, (void *)-1) == &v4);
  printf("  [INFO] PtrHashMap: Stored NULL and (void*)-1 successfully.\n");

  // Remove
  ptr_hashmap_remove(map, k1);
  TEST_ASSERT(ptr_hashmap_size(map) == 3);
  TEST_ASSERT(ptr_hashmap_get(map, k1) == NULL);
  TEST_ASSERT(ptr_hashmap_contains(map, NULL) == true);
}

/**
 * @brief 测试 StrHashMap
 */
void
test_str_hashmap()
{
  StrHashMap *map = str_hashmap_create(&global_arena, 8);
  TEST_ASSERT(map != NULL);

  int v1 = 1, v2 = 2, v3 = 3;
  str_hashmap_put(map, "hello", 5, &v1);
  str_hashmap_put(map, "world", 5, &v2);
  TEST_ASSERT(str_hashmap_size(map) == 2);
  TEST_ASSERT(str_hashmap_get(map, "hello", 5) == &v1);

  // Test get with different pointer, same content
  char key_copy[] = "hello";
  TEST_ASSERT(str_hashmap_get(map, key_copy, 5) == &v1);
  printf("  [INFO] StrHashMap: Passed value-based lookup.\n");

  // Test empty string
  str_hashmap_put(map, "", 0, &v3);
  TEST_ASSERT(str_hashmap_size(map) == 3);
  TEST_ASSERT(str_hashmap_get(map, "", 0) == &v3);

  // Test remove
  str_hashmap_remove(map, "hello", 5);
  TEST_ASSERT(str_hashmap_size(map) == 2);
  TEST_ASSERT(str_hashmap_get(map, "hello", 5) == NULL);
  TEST_ASSERT(str_hashmap_contains(map, "world", 5) == true);
}

/**
 * @brief 测试 F64HashMap
 *
 * 关键测试: 验证 Inf 是合法 key, NaN 仍然不可用。
 */
void
test_float_hashmap()
{
  F64HashMap *map = f64_hashmap_create(&global_arena, 8);
  TEST_ASSERT(map != NULL);

  int v1 = 1, v_inf = 100, v_ninf = 200, v_zero = 0;

  f64_hashmap_put(map, 123.456, &v1);

  // 0.0 和 -0.0
  f64_hashmap_put(map, 0.0, &v_zero);
  TEST_ASSERT(f64_hashmap_size(map) == 2); // 123.456 和 0.0
  f64_hashmap_put(map, -0.0, &v1);         // k1 == k2 认为 0.0 == -0.0
  TEST_ASSERT(f64_hashmap_size(map) == 2); // Size 不变
  printf("  [DEBUG] f64_hashmap_size(map): %zu\n", f64_hashmap_size(map));
  TEST_ASSERT(f64_hashmap_get(map, 0.0) == &v1);
  printf("  [DEBUG] f64_hashmap_get(map, 0.0): %p, &v1: %p\n", f64_hashmap_get(map, 0.0), &v1);
  TEST_ASSERT(f64_hashmap_get(map, -0.0) == &v1); // 应该能找到

  // [关键] 测试旧的哨兵值 (Inf)
  f64_hashmap_put(map, INFINITY, &v_inf);
  f64_hashmap_put(map, -INFINITY, &v_ninf);

  TEST_ASSERT(f64_hashmap_size(map) == 4);
  printf("  [DEBUG] f64_hashmap_size(map): %zu\n", f64_hashmap_size(map));
  TEST_ASSERT(f64_hashmap_get(map, INFINITY) == &v_inf);
  TEST_ASSERT(f64_hashmap_get(map, -INFINITY) == &v_ninf);
  printf("  [INFO] F64HashMap: Stored INFINITY and -INFINITY successfully.\n");

  // [关键] 测试 NaN (NaN != NaN)
  // put(nan) 应该 assert fail (在 debug 模式下), 我们只测试 get/contains
  TEST_ASSERT(f64_hashmap_get(map, nan("")) == NULL);
  TEST_ASSERT(f64_hashmap_contains(map, nan("")) == false);
  printf("  [INFO] F64HashMap: Correctly failed to find NaN.\n");
}

/**
 * @brief 测试 GenericHashMap (用于 Structs)
 *
 * 关键测试: 验证自定义 hash/equal 函数是否被调用。
 */

// 1. 为 GenericHashMap 定义一个自定义 struct
typedef struct
{
  int id;
  const char *tag;
} MyStructKey;

// 2. 定义 hash_fn
uint64_t
my_struct_hash(const void *key)
{
  const MyStructKey *k = (const MyStructKey *)key;
  // 简单的哈希:
  // (我们假设 xxhash.h 已经被包含了, 并且可以被 test_hashmap.c 链接)
  // 如果 xxhash.h 是 "inline all", 我们需要在这里再次包含它...
  // 为了简单起见, 我们先用一个简单的 xor 哈希
  uint64_t tag_hash = 0;
  if (k->tag)
  {
    // 简单的 FNV-1a 变种
    tag_hash = 14695981039346656037ULL;
    for (const char *p = k->tag; *p; p++)
    {
      tag_hash ^= (uint64_t)(*p);
      tag_hash *= 1099511628211ULL;
    }
  }
  return (uint64_t)k->id ^ tag_hash;
}

// 3. 定义 equal_fn
bool
my_struct_equal(const void *key1, const void *key2)
{
  const MyStructKey *k1 = (const MyStructKey *)key1;
  const MyStructKey *k2 = (const MyStructKey *)key2;

  if (k1->id != k2->id)
    return false;

  if (k1->tag == k2->tag)
    return true; // 相同指针
  if (k1->tag == NULL || k2->tag == NULL)
    return false; // 一个为 null

  return (strcmp(k1->tag, k2->tag) == 0);
}

void
test_generic_hashmap()
{
  GenericHashMap *map = generic_hashmap_create(&global_arena, 8, my_struct_hash, my_struct_equal);
  TEST_ASSERT(map != NULL);

  int v1 = 10, v2 = 20;

  // Key 必须存储在稳定内存中 (Arena), 因为 map 只存指针
  MyStructKey *k1 = BUMP_ALLOC(&global_arena, MyStructKey);
  k1->id = 1;
  k1->tag = "TypeA";

  MyStructKey *k2 = BUMP_ALLOC(&global_arena, MyStructKey);
  k2->id = 2;
  k2->tag = "TypeB";

  generic_hashmap_put(map, k1, &v1);
  generic_hashmap_put(map, k2, &v2);
  TEST_ASSERT(generic_hashmap_size(map) == 2);
  TEST_ASSERT(generic_hashmap_get(map, k1) == &v1);

  // [关键] 测试值相等, 但指针不同
  // 创建一个*栈上*的 key, 其内容与 k1 相同
  MyStructKey stack_key;
  stack_key.id = 1;
  stack_key.tag = "TypeA";

  TEST_ASSERT(generic_hashmap_get(map, &stack_key) == &v1);
  TEST_ASSERT(generic_hashmap_contains(map, &stack_key) == true);
  printf("  [INFO] GenericHashMap: Passed value-based lookup (stack key).\n");

  // 测试 Overwrite (使用 stack_key)
  generic_hashmap_put(map, &stack_key, &v2);
  TEST_ASSERT(generic_hashmap_size(map) == 2);      // Size 不变
  TEST_ASSERT(generic_hashmap_get(map, k1) == &v2); // k1 应该被更新

  // 测试 Remove (使用 stack_key)
  stack_key.id = 2; // 指向 k2
  stack_key.tag = "TypeB";
  bool removed = generic_hashmap_remove(map, &stack_key);
  TEST_ASSERT(removed == true);
  TEST_ASSERT(generic_hashmap_size(map) == 1);
  TEST_ASSERT(generic_hashmap_get(map, k2) == NULL);
  TEST_ASSERT(generic_hashmap_get(map, k1) == &v2); // k1 还在
}

// 3. --- Main 函数 ---

int
main(void)
{
  // --- [已根据你的 bump.h 修改] ---
  // 初始化一个全局 Arena 供所有测试使用
  // (根据你的 bump.h, init 不带参数)
  bump_init(&global_arena);

  printf("=== Calir HashMap Test Suite ===\n");
  printf("  (Testing 'states' refactor and 'generic' map)\n");

  RUN_TEST(test_int_hashmap);
  RUN_TEST(test_ptr_hashmap);
  RUN_TEST(test_str_hashmap);
  RUN_TEST(test_float_hashmap);
  RUN_TEST(test_generic_hashmap);

  printf("\n==================================\n");
  printf("  TOTAL TESTS: %d\n", total_tests);
  printf("  TOTAL FAILS: %d\n", total_fails);
  printf("==================================\n");

  // --- [已根据你的 bump.h 修改] ---
  // 销毁 Arena, 释放其分配的所有内存块
  // (这适用于由 bump_init 初始化的栈/全局 Arena)
  bump_destroy(&global_arena);

  // 如果有失败, 返回非零退出码
  return (total_fails > 0) ? 1 : 0;
}

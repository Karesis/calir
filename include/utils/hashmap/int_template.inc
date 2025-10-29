/*
 * utils/hashmap/int_template.inc
 *
 * 为单一整数类型生成完整的哈希表实现。
 * * * 在包含此文件之前, 必须定义以下宏:
 *
 * - INT_PREFIX:      函数前缀 (例如: i64)
 * - INT_K_TYPE:      Key 的类型 (例如: int64_t)
 * - INT_V_TYPE:      Value 的类型 (void*)
 * - INT_API_TYPE:    公开的 Map 类型 (例如: I64HashMap)
 * - INT_STRUCT_TYPE: 内部的 Map 结构体类型 (例如: I64HashMap_t)
 * - INT_BUCKET_TYPE: 内部的 Bucket 结构体类型 (例如: I64HashMapBucket)
 */

// 包含状态定义
#include "utils/hashmap/common.h"

// --- 宏工具 (在 int.c 中定义) ---
// #define _INT_PASTE3(a, b, c) a##b##c
// #define INT_PASTE3(a, b, c) _INT_PASTE3(a, b, c)
// #define INT_FUNC(suffix) INT_PASTE3(INT_PREFIX, _hashmap_, suffix)

/*
 * ========================================
 * --- 1. 类型和结构体定义 ---
 * ========================================
 */

typedef struct
{
  INT_K_TYPE key;
  INT_V_TYPE value;
} INT_BUCKET_TYPE;

struct INT_STRUCT_TYPE
{
  Bump *arena;
  INT_BUCKET_TYPE *buckets;
  uint8_t *states; // 状态数组
  size_t num_entries;
  size_t num_tombstones;
  size_t num_buckets;
};

/*
 * ========================================
 * --- 2. "Trait" 函数实现 ---
 * ========================================
 */

static inline bool
INT_FUNC(key_is_equal)(INT_K_TYPE k1, INT_K_TYPE k2)
{
  return k1 == k2;
}

static inline uint64_t
INT_FUNC(get_hash)(INT_K_TYPE key)
{
  // 对整数本身进行哈希
  return XXH3_64bits(&key, sizeof(key));
}

/*
 * ========================================
 * --- 3. 包含泛型核心实现 ---
 * ========================================
 */

#define CHM_PREFIX INT_PREFIX
#define CHM_K_TYPE INT_K_TYPE
#define CHM_V_TYPE INT_V_TYPE
#define CHM_API_TYPE INT_API_TYPE
#define CHM_STRUCT_TYPE INT_STRUCT_TYPE
#define CHM_BUCKET_TYPE INT_BUCKET_TYPE

#include "utils/hashmap/core.inc"

/*
 * ========================================
 * --- 4. 公共 API 实现 ---
 * ========================================
 */

INT_API_TYPE *
INT_FUNC(create)(Bump *arena, size_t initial_capacity)
{
  assert(arena != NULL && "Bump arena cannot be NULL");
  size_t num_buckets = INT_FUNC(get_min_buckets_for_entries)(initial_capacity);

  INT_API_TYPE *map = BUMP_ALLOC(arena, INT_STRUCT_TYPE);
  if (!map)
    return NULL;

  // Buckets 不需要清零, 状态由 'states' 数组管理
  INT_BUCKET_TYPE *buckets = BUMP_ALLOC_SLICE(arena, INT_BUCKET_TYPE, num_buckets);
  if (!buckets)
    return NULL;

  // States *必须* 清零 (BUMP_ALLOC_SLICE_ZEROED)
  // 因为 BUCKET_EMPTY == 0
  uint8_t *states = BUMP_ALLOC_SLICE_ZEROED(arena, uint8_t, num_buckets);
  if (!states)
    return NULL; // OOM

  map->arena = arena;
  map->buckets = buckets;
  map->states = states; // <-- 设置 states 指针
  map->num_entries = 0;
  map->num_tombstones = 0;
  map->num_buckets = num_buckets;

  return map;
}

void *
INT_FUNC(get)(const INT_API_TYPE *map, INT_K_TYPE key)
{
  INT_BUCKET_TYPE *bucket;
  if (INT_FUNC(find_bucket)(map, key, &bucket))
  {
    return bucket->value;
  }
  return NULL;
}

bool
INT_FUNC(contains)(const INT_API_TYPE *map, INT_K_TYPE key)
{
  INT_BUCKET_TYPE *bucket;
  return INT_FUNC(find_bucket)(map, key, &bucket);
}

bool
INT_FUNC(remove)(INT_API_TYPE *map, INT_K_TYPE key)
{
  INT_BUCKET_TYPE *bucket;
  if (INT_FUNC(find_bucket)(map, key, &bucket))
  {
    // Key 存在, 将其槽位标记为墓碑
    size_t bucket_idx = (size_t)(bucket - map->buckets);
    map->states[bucket_idx] = BUCKET_TOMBSTONE;

    bucket->value = NULL; // (可选, 但良好实践)

    map->num_entries--;
    map->num_tombstones++;
    return true;
  }
  return false; // Key 不存在
}

bool
INT_FUNC(put)(INT_API_TYPE *map, INT_K_TYPE key, void *value)
{
  INT_BUCKET_TYPE *bucket;
  bool found = INT_FUNC(find_bucket)(map, key, &bucket);

  if (found)
  {
    // Key 已存在, 更新 value
    bucket->value = value;
    return true;
  }

  // Key 不存在, 'bucket' 指向我们应该插入的槽位
  assert(bucket != NULL && "find_bucket must return a valid slot");

  // 检查是否需要扩容
  size_t total_load = map->num_entries + map->num_tombstones + 1;
  if (total_load * 4 >= map->num_buckets * 3)
  {
    if (!INT_FUNC(grow)(map))
    {
      return false; // OOM on grow
    }
    // 扩容后, 必须重新查找槽位
    found = INT_FUNC(find_bucket)(map, key, &bucket);
    assert(!found && "Key should not exist after grow");
    assert(bucket != NULL);
  }

  size_t bucket_idx = (size_t)(bucket - map->buckets);

  // 如果我们复用了墓碑, 减少墓碑计数
  if (map->states[bucket_idx] == BUCKET_TOMBSTONE)
  {
    map->num_tombstones--;
  }

  // 插入新条目
  bucket->key = key;
  bucket->value = value;
  map->states[bucket_idx] = BUCKET_FILLED; // <-- 标记为 FILLED
  map->num_entries++;

  return true;
}

size_t
INT_FUNC(size)(const INT_API_TYPE *map)
{
  return map->num_entries;
}

/*
 * ========================================
 * --- 5. (修正) 迭代器 API 实现 ---
 * ========================================
 */

// 1. (关键) 定义 CHM_FUNC, 重用 int.c 中定义的 INT_PASTE3
//    (INT_PASTE3 和 INT_FUNC 已经在 int.c 中定义并可用)
#define CHM_FUNC(prefix, suffix) INT_FUNC(suffix)

// 2. (关键) 定义我们需要的两步粘贴宏 (用于 Entry/Iter 类型名)
#define _CHM_PASTE(a, b) a##b
#define CHM_PASTE(a, b) _CHM_PASTE(a, b)

// 3. 为 iterator.inc 设置 "模板参数"
#define CHM_PREFIX INT_PREFIX
#define CHM_API_TYPE INT_API_TYPE
#define CHM_STRUCT_TYPE INT_STRUCT_TYPE
#define CHM_BUCKET_TYPE INT_BUCKET_TYPE

// 4. 使用 CHM_PASTE 来正确展开 API_TYPE
//    INT_API_TYPE##Entry -> (I64HashMap)Entry -> I64HashMapEntry
#define CHM_ENTRY_TYPE CHM_PASTE(INT_API_TYPE, Entry)
#define CHM_ITER_TYPE CHM_PASTE(INT_API_TYPE, Iter)

// 5. 包含 *已清理* 的通用实现
#include "utils/hashmap/iterator.inc"

// 6. 清理 *本节* 定义的宏
#undef CHM_FUNC
#undef _CHM_PASTE
#undef CHM_PASTE

/*
 * ========================================
 * --- 6. 清理宏 ---
 * ========================================
 */
#undef INT_PREFIX
#undef INT_K_TYPE
#undef INT_V_TYPE
#undef INT_API_TYPE
#undef INT_STRUCT_TYPE
#undef INT_BUCKET_TYPE
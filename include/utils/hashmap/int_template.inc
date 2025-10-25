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
 * - INT_EMPTY_K:     作为 'Empty' 哨兵的 Key 值 (例如: (int64_t)0)
 * - INT_TOMBSTONE_K: 作为 'Tombstone' 哨兵的 Key 值 (例如: (int64_t)-1)
 */

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
  size_t num_entries;
  size_t num_tombstones;
  size_t num_buckets;
};

/*
 * ========================================
 * --- 2. "Trait" 函数实现 ---
 * ========================================
 */

static INT_K_TYPE
INT_FUNC(get_empty_key)(void)
{
  return INT_EMPTY_K;
}

static INT_K_TYPE
INT_FUNC(get_tombstone_key)(void)
{
  return INT_TOMBSTONE_K;
}

static inline bool
INT_FUNC(key_is_equal)(INT_K_TYPE k1, INT_K_TYPE k2)
{
  return k1 == k2;
}

static inline bool
INT_FUNC(key_is_empty)(INT_K_TYPE k)
{
  return k == INT_EMPTY_K;
}

static inline bool
INT_FUNC(key_is_tombstone)(INT_K_TYPE k)
{
  return k == INT_TOMBSTONE_K;
}

static inline bool
INT_FUNC(key_is_sentinel)(INT_K_TYPE k)
{
  return k == INT_EMPTY_K || k == INT_TOMBSTONE_K;
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

  INT_BUCKET_TYPE *buckets = BUMP_ALLOC_SLICE_ZEROED(arena, INT_BUCKET_TYPE, num_buckets);
  if (!buckets)
    return NULL;

  map->arena = arena;
  map->buckets = buckets;
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
    bucket->key = INT_TOMBSTONE_K;
    bucket->value = NULL;
    map->num_entries--;
    map->num_tombstones++;
    return true;
  }
  return false;
}

bool
INT_FUNC(put)(INT_API_TYPE *map, INT_K_TYPE key, void *value)
{
  // 检查哨兵值
  assert(key != INT_EMPTY_K && "Key cannot be the 'Empty' sentinel");
  assert(key != INT_TOMBSTONE_K && "Key cannot be the 'Tombstone' sentinel");

  INT_BUCKET_TYPE *bucket;
  bool found = INT_FUNC(find_bucket)(map, key, &bucket);

  if (found)
  {
    bucket->value = value;
    return true;
  }

  assert(bucket != NULL && "find_bucket must return a valid slot");

  size_t total_load = map->num_entries + map->num_tombstones + 1;
  if (total_load * 4 >= map->num_buckets * 3)
  {
    if (!INT_FUNC(grow)(map))
    {
      return false; // OOM on grow
    }
    found = INT_FUNC(find_bucket)(map, key, &bucket);
    assert(!found && "Key should not exist after grow");
    assert(bucket != NULL);
  }

  if (INT_FUNC(key_is_tombstone)(bucket->key))
  {
    map->num_tombstones--;
  }

  // 整数 Key, 直接存储
  bucket->key = key;
  bucket->value = value;
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
 * --- 5. 清理宏 ---
 * ========================================
 */
#undef INT_PREFIX
#undef INT_K_TYPE
#undef INT_V_TYPE
#undef INT_API_TYPE
#undef INT_STRUCT_TYPE
#undef INT_BUCKET_TYPE
#undef INT_EMPTY_K
#undef INT_TOMBSTONE_K
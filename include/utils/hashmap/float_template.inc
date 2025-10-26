/*
 * utils/hashmap/float_template.inc
 *
 * 为单一浮点类型生成完整的哈希表实现。
 * * * 在包含此文件之前, 必须定义以下宏:
 *
 * - FLOAT_PREFIX:      函数前缀 (例如: f32)
 * - FLOAT_K_TYPE:      Key 的类型 (例如: float)
 * - FLOAT_V_TYPE:      Value 的类型 (void*)
 * - FLOAT_API_TYPE:    公开的 Map 类型 (例如: F32HashMap)
 * - FLOAT_STRUCT_TYPE: 内部的 Map 结构体类型 (例如: F32HashMap_t)
 * - FLOAT_BUCKET_TYPE: 内部的 Bucket 结构体类型 (例如: F32HashMapBucket)
 */

#include "utils/hashmap/common.h"
#include <assert.h>
#include <math.h>

/*
 * ========================================
 * --- 1. 类型和结构体定义 ---
 * ========================================
 */

typedef struct
{
  FLOAT_K_TYPE key;
  FLOAT_V_TYPE value;
} FLOAT_BUCKET_TYPE;

struct FLOAT_STRUCT_TYPE
{
  Bump *arena;
  FLOAT_BUCKET_TYPE *buckets;
  uint8_t *states;
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
FLOAT_FUNC(key_is_equal)(FLOAT_K_TYPE k1, FLOAT_K_TYPE k2)
{
  // 正常的浮点数比较。
  // 'Inf == Inf' 为 true (可以被找到)
  // 'NaN == NaN' 为 false (不能被找到, 这就是为什么 'put' 必须禁止 NaN)
  return k1 == k2;
}

static inline uint64_t
FLOAT_FUNC(get_hash)(FLOAT_K_TYPE key)
{
  // 哈希浮点数的位模式 (bit pattern)
  return XXH3_64bits(&key, sizeof(key));
}

/*
 * ========================================
 * --- 3. 包含泛型核心实现 ---
 * ========================================
 */

#define CHM_PREFIX FLOAT_PREFIX
#define CHM_K_TYPE FLOAT_K_TYPE
#define CHM_V_TYPE FLOAT_V_TYPE
#define CHM_API_TYPE FLOAT_API_TYPE
#define CHM_STRUCT_TYPE FLOAT_STRUCT_TYPE
#define CHM_BUCKET_TYPE FLOAT_BUCKET_TYPE

#include "utils/hashmap/core.inc"

/*
 * ========================================
 * --- 4. 公共 API 实现 ---
 * ========================================
 */

FLOAT_API_TYPE *
FLOAT_FUNC(create)(Bump *arena, size_t initial_capacity)
{
  assert(arena != NULL && "Bump arena cannot be NULL");
  size_t num_buckets = FLOAT_FUNC(get_min_buckets_for_entries)(initial_capacity);

  FLOAT_API_TYPE *map = BUMP_ALLOC(arena, FLOAT_STRUCT_TYPE);
  if (!map)
    return NULL;

  // Buckets 不需要清零
  FLOAT_BUCKET_TYPE *buckets = BUMP_ALLOC_SLICE(arena, FLOAT_BUCKET_TYPE, num_buckets);
  if (!buckets)
    return NULL;

  // States *必须* 清零
  uint8_t *states = BUMP_ALLOC_SLICE_ZEROED(arena, uint8_t, num_buckets);
  if (!states)
    return NULL;

  map->arena = arena;
  map->buckets = buckets;
  map->states = states; // <-- 设置 states 指针
  map->num_entries = 0;
  map->num_tombstones = 0;
  map->num_buckets = num_buckets;

  return map;
}

void *
FLOAT_FUNC(get)(const FLOAT_API_TYPE *map, FLOAT_K_TYPE key)
{
  // 'get' 一个 NaN 键将永远返回 NULL, 这是符合逻辑的
  FLOAT_BUCKET_TYPE *bucket;
  if (FLOAT_FUNC(find_bucket)(map, key, &bucket))
  {
    return bucket->value;
  }
  return NULL;
}

bool
FLOAT_FUNC(contains)(const FLOAT_API_TYPE *map, FLOAT_K_TYPE key)
{
  // 'contains' 一个 NaN 键将永远返回 false
  FLOAT_BUCKET_TYPE *bucket;
  return FLOAT_FUNC(find_bucket)(map, key, &bucket);
}

bool
FLOAT_FUNC(remove)(FLOAT_API_TYPE *map, FLOAT_K_TYPE key)
{
  // 'remove' 一个 NaN 键将永远返回 false
  FLOAT_BUCKET_TYPE *bucket;
  if (FLOAT_FUNC(find_bucket)(map, key, &bucket))
  {
    // Key 存在, 将其槽位标记为墓碑
    size_t bucket_idx = (size_t)(bucket - map->buckets);
    map->states[bucket_idx] = BUCKET_TOMBSTONE;

    bucket->value = NULL;
    map->num_entries--;
    map->num_tombstones++;
    return true;
  }
  return false;
}

bool
FLOAT_FUNC(put)(FLOAT_API_TYPE *map, FLOAT_K_TYPE key, void *value)
{
  // 检查哨兵值
  assert(!isnan(key) && "Key cannot be NaN (due to 'NaN != NaN' comparison rule)");

  FLOAT_BUCKET_TYPE *bucket;
  bool found = FLOAT_FUNC(find_bucket)(map, key, &bucket);

  if (found)
  {
    bucket->value = value;
    return true;
  }

  assert(bucket != NULL && "find_bucket must return a valid slot");

  size_t total_load = map->num_entries + map->num_tombstones + 1;
  if (total_load * 4 >= map->num_buckets * 3)
  {
    if (!FLOAT_FUNC(grow)(map))
    {
      return false; // OOM on grow
    }
    found = FLOAT_FUNC(find_bucket)(map, key, &bucket);
    assert(!found && "Key should not exist after grow");
    assert(bucket != NULL);
  }

  size_t bucket_idx = (size_t)(bucket - map->buckets);

  // 检查是否复用墓碑
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
FLOAT_FUNC(size)(const FLOAT_API_TYPE *map)
{
  return map->num_entries;
}

/*
 * ========================================
 * --- 5. 清理宏 ---
 * ========================================
 */
#undef FLOAT_PREFIX
#undef FLOAT_K_TYPE
#undef FLOAT_V_TYPE
#undef FLOAT_API_TYPE
#undef FLOAT_STRUCT_TYPE
#undef FLOAT_BUCKET_TYPE
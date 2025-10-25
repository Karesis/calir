/*
 * src/hashmap/float_template.inc
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
 * - FLOAT_EMPTY_K:     作为 'Empty' 哨兵的 Key 值 (例如: nanf(""))
 * - FLOAT_TOMBSTONE_K: 作为 'Tombstone' 哨兵的 Key 值 (例如: INFINITY)
 */

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
  size_t num_entries;
  size_t num_tombstones;
  size_t num_buckets;
};

/*
 * ========================================
 * --- 2. "Trait" 函数实现 ---
 * ========================================
 */

static FLOAT_K_TYPE
FLOAT_FUNC(get_empty_key)(void) { return FLOAT_EMPTY_K; }

static FLOAT_K_TYPE
FLOAT_FUNC(get_tombstone_key)(void) { return FLOAT_TOMBSTONE_K; }

static inline bool
FLOAT_FUNC(key_is_equal)(FLOAT_K_TYPE k1, FLOAT_K_TYPE k2)
{
  // 正常的浮点数比较。
  // NaN == NaN 会返回 false, is_empty() 会处理 NaN。
  // Inf == Inf 会返回 true, is_tombstone() 会处理 Inf。
  return k1 == k2;
}

static inline bool
FLOAT_FUNC(key_is_empty)(FLOAT_K_TYPE k) { return isnan(k); }

static inline bool
FLOAT_FUNC(key_is_tombstone)(FLOAT_K_TYPE k) { return isinf(k); }

static inline bool
FLOAT_FUNC(key_is_sentinel)(FLOAT_K_TYPE k)
{
  return isnan(k) || isinf(k);
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

#include "hashmap/core.inc"

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

  FLOAT_BUCKET_TYPE *buckets = BUMP_ALLOC_SLICE_ZEROED(arena, FLOAT_BUCKET_TYPE, num_buckets);
  if (!buckets)
    return NULL;

  // 用 'Empty' (NaN) 哨兵值填充
  for (size_t i = 0; i < num_buckets; i++)
  {
    buckets[i].key = FLOAT_EMPTY_K;
  }

  map->arena = arena;
  map->buckets = buckets;
  map->num_entries = 0;
  map->num_tombstones = 0;
  map->num_buckets = num_buckets;

  return map;
}

void *
FLOAT_FUNC(get)(const FLOAT_API_TYPE *map, FLOAT_K_TYPE key)
{
  FLOAT_BUCKET_TYPE *bucket;
  if (FLOAT_FUNC(find_bucket)(map, key, &bucket))
  {
    return bucket->value;
  }
  return NULL;
}

bool FLOAT_FUNC(contains)(const FLOAT_API_TYPE *map, FLOAT_K_TYPE key)
{
  FLOAT_BUCKET_TYPE *bucket;
  return FLOAT_FUNC(find_bucket)(map, key, &bucket);
}

bool FLOAT_FUNC(remove)(FLOAT_API_TYPE *map, FLOAT_K_TYPE key)
{
  FLOAT_BUCKET_TYPE *bucket;
  if (FLOAT_FUNC(find_bucket)(map, key, &bucket))
  {
    bucket->key = FLOAT_TOMBSTONE_K; // Tombstone (Inf)
    bucket->value = NULL;
    map->num_entries--;
    map->num_tombstones++;
    return true;
  }
  return false;
}

bool FLOAT_FUNC(put)(FLOAT_API_TYPE *map, FLOAT_K_TYPE key, void *value)
{
  // 检查哨兵值
  assert(!isnan(key) && "Key cannot be NaN (reserved for 'Empty')");
  assert(!isinf(key) && "Key cannot be Inf (reserved for 'Tombstone')");

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

  if (FLOAT_FUNC(key_is_tombstone)(bucket->key))
  {
    map->num_tombstones--;
  }

  bucket->key = key;
  bucket->value = value;
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
#undef FLOAT_EMPTY_K
#undef FLOAT_TOMBSTONE_K
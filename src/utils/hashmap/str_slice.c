/* utils/hashmap/str_slice.c */
#include "utils/hashmap/str_slice.h"
#include "utils/bump.h"
#include <assert.h>
#include <string.h> // for memcmp, memcpy

// 1. 包含 xxhash.h 并内联实现
#define XXH_INLINE_ALL
#include "utils/xxhash.h"

#include "utils/hashmap/common.h"

/*
 * ========================================
 * --- 1. 类型和结构体定义 ---
 * ========================================
 */

// 内部使用的字符串切片 (StrSlice)
typedef struct
{
  const char *body;
  size_t len;
} StrSlice;

// 哈希表桶 (Bucket)
typedef struct
{
  StrSlice key;
  void *value;
} StrHashMapBucket;

// StrHashMap 结构体的完整定义
struct StrHashMap
{
  Bump *arena; // 用于所有分配
  StrHashMapBucket *buckets;
  uint8_t *states;
  size_t num_entries;
  size_t num_tombstones;
  size_t num_buckets; // 必须始终是 2 的幂
};

/*
 * ========================================
 * --- 2. "Trait" 函数实现 ---
 * ========================================
 */

static inline bool
str_hashmap_key_is_equal(StrSlice k1, StrSlice k2)
{
  if (k1.len != k2.len)
    return false;
  if (k1.body == k2.body)
    return true;

  return (memcmp(k1.body, k2.body, k1.len) == 0);
}

static inline uint64_t
str_hashmap_get_hash(StrSlice key)
{
  return XXH3_64bits(key.body, key.len);
}

/*
 * ========================================
 * --- 3. 包含泛型核心实现 ---
 * ========================================
 */

// 定义模板参数
#define CHM_PREFIX str
#define CHM_K_TYPE StrSlice
#define CHM_V_TYPE void *
#define CHM_API_TYPE StrHashMap
#define CHM_STRUCT_TYPE StrHashMap
#define CHM_BUCKET_TYPE StrHashMapBucket

// 实例化泛型函数
// 这将定义:
// - str_hashmap_next_pow2
// - str_hashmap_get_min_buckets_for_entries
// - str_hashmap_find_bucket
// - str_hashmap_grow
#include "utils/hashmap/core.inc"

/*
 * ========================================
 * --- 4. 公共 API 实现 ---
 * (这些函数是特化的, 它们调用泛型核心)
 * ========================================
 */

StrHashMap *
str_hashmap_create(Bump *arena, size_t initial_capacity)
{
  assert(arena != NULL && "Bump arena cannot be NULL");
  size_t num_buckets = str_hashmap_get_min_buckets_for_entries(initial_capacity);

  StrHashMap *map = BUMP_ALLOC(arena, StrHashMap);
  if (!map)
    return NULL;

  // Buckets 不需要清零
  StrHashMapBucket *buckets = BUMP_ALLOC_SLICE(arena, StrHashMapBucket, num_buckets);
  if (!buckets)
    return NULL;

  // States *必须* 清零
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
str_hashmap_get(const StrHashMap *map, const char *key_body, size_t key_len)
{
  // 将 (char*, len) 转换为内部 StrSlice
  StrSlice key_to_find = {.body = key_body, .len = key_len};
  StrHashMapBucket *bucket;

  // 调用泛型的 find_bucket
  if (str_hashmap_find_bucket(map, key_to_find, &bucket))
  {
    return bucket->value;
  }
  return NULL;
}

bool
str_hashmap_contains(const StrHashMap *map, const char *key_body, size_t key_len)
{
  StrSlice key_to_find = {.body = key_body, .len = key_len};
  StrHashMapBucket *bucket;
  return str_hashmap_find_bucket(map, key_to_find, &bucket);
}

bool
str_hashmap_remove(StrHashMap *map, const char *key_body, size_t key_len)
{
  StrSlice key_to_find = {.body = key_body, .len = key_len};
  StrHashMapBucket *bucket;

  if (str_hashmap_find_bucket(map, key_to_find, &bucket))
  {
    // Key 存在, 将其槽位标记为墓碑
    size_t bucket_idx = (size_t)(bucket - map->buckets);
    map->states[bucket_idx] = BUCKET_TOMBSTONE;

    bucket->value = NULL; // (良好实践)
    map->num_entries--;
    map->num_tombstones++;
    return true;
  }
  return false; // Key 不存在
}

bool
str_hashmap_put(StrHashMap *map, const char *key_body, size_t key_len, void *value)
{
  StrSlice key_to_find = {.body = key_body, .len = key_len};
  StrHashMapBucket *bucket;

  bool found = str_hashmap_find_bucket(map, key_to_find, &bucket);

  if (found)
  {
    bucket->value = value;
    return true;
  }

  assert(bucket != NULL && "find_bucket must return a valid slot");

  size_t total_load = map->num_entries + map->num_tombstones + 1;
  if (total_load * 4 >= map->num_buckets * 3)
  {
    if (!str_hashmap_grow(map))
    {
      return false; // OOM on grow
    }
    found = str_hashmap_find_bucket(map, key_to_find, &bucket);
    assert(!found && "Key should not exist after grow");
    assert(bucket != NULL);
  }

  size_t bucket_idx = (size_t)(bucket - map->buckets);

  // 检查是否复用墓碑
  if (map->states[bucket_idx] == BUCKET_TOMBSTONE)
  {
    map->num_tombstones--;
  }

  // --- 这是 `str_slice` 的特化逻辑 ---
  // 复制 Key 到 Arena 中
  const char *new_key_body = bump_alloc_copy(map->arena, key_body, key_len, 1);
  if (!new_key_body && key_len > 0)
  {
    return false; // OOM on key copy
  }

  bucket->key.body = new_key_body;
  bucket->key.len = key_len;
  bucket->value = value;
  map->states[bucket_idx] = BUCKET_FILLED; // <-- 标记为 FILLED
  map->num_entries++;

  return true;
}

bool
str_hashmap_put_preallocated_key(StrHashMap *map, const char *key_body, size_t key_len, void *value)
{
  StrSlice key_to_find = {.body = key_body, .len = key_len};
  StrHashMapBucket *bucket;

  bool found = str_hashmap_find_bucket(map, key_to_find, &bucket);

  if (found)
  {
    bucket->value = value;
    return true;
  }

  assert(bucket != NULL && "find_bucket must return a valid slot");

  size_t total_load = map->num_entries + map->num_tombstones + 1;
  if (total_load * 4 >= map->num_buckets * 3)
  {
    if (!str_hashmap_grow(map))
    {
      return false; // OOM on grow
    }
    found = str_hashmap_find_bucket(map, key_to_find, &bucket);
    assert(!found && "Key should not exist after grow");
    assert(bucket != NULL);
  }

  size_t bucket_idx = (size_t)(bucket - map->buckets);

  // 检查是否复用墓碑
  if (map->states[bucket_idx] == BUCKET_TOMBSTONE)
  {
    map->num_tombstones--;
  }

  // 我们信任 key_body 已经 在 arena 中，并且是唯一的。
  // 我们 *不* 复制它，只存储指针。
  bucket->key.body = key_body;
  bucket->key.len = key_len;
  bucket->value = value;
  map->states[bucket_idx] = BUCKET_FILLED; // <-- 标记为 FILLED
  map->num_entries++;

  return true;
}

size_t
str_hashmap_size(const StrHashMap *map)
{
  return map->num_entries;
}
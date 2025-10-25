/* src/hashmap/ptr.c */
#include "utils/hashmap/ptr.h"
#include "utils/bump.h"
#include <assert.h>
#include <string.h> // for memcmp, memcpy

#define XXH_INLINE_ALL
#include "utils/xxhash.h"

/*
 * ========================================
 * --- 1. 类型和结构体定义 ---
 * ========================================
 */

typedef struct
{
  void *key;
  void *value;
} PtrHashMapBucket;

// PtrHashMap 结构体的完整定义
struct PtrHashMap
{
  Bump *arena;
  PtrHashMapBucket *buckets;
  size_t num_entries;
  size_t num_tombstones;
  size_t num_buckets;
};

/*
 * ========================================
 * --- 2. "Trait" 函数实现 ---
 * ========================================
 */

static void *
ptr_hashmap_get_empty_key(void)
{
  return NULL;
}

static void *
ptr_hashmap_get_tombstone_key(void)
{
  return (void *)-1;
}

static inline bool
ptr_hashmap_key_is_equal(void *k1, void *k2)
{
  return k1 == k2;
}

static inline bool
ptr_hashmap_key_is_empty(void *k)
{
  return k == NULL;
}

static inline bool
ptr_hashmap_key_is_tombstone(void *k)
{
  return k == (void *)-1;
}

static inline bool
ptr_hashmap_key_is_sentinel(void *k)
{
  return k == NULL || k == (void *)-1;
}

static inline uint64_t
ptr_hashmap_get_hash(void *key)
{
  return XXH3_64bits(&key, sizeof(void *));
}

/*
 * ========================================
 * --- 3. 包含泛型核心实现 ---
 * ========================================
 */

// 定义模板参数
#define CHM_PREFIX ptr
#define CHM_K_TYPE void *
#define CHM_V_TYPE void *
#define CHM_API_TYPE PtrHashMap
#define CHM_STRUCT_TYPE PtrHashMap_t
#define CHM_BUCKET_TYPE PtrHashMapBucket

// 实例化泛型函数
// 这将定义:
// - ptr_hashmap_next_pow2
// - ptr_hashmap_get_min_buckets_for_entries
// - ptr_hashmap_find_bucket
// - ptr_hashmap_grow
#include "utils/hashmap/core.inc"

/*
 * ========================================
 * --- 4. 公共 API 实现 ---
 * (这些函数是特化的, 它们调用泛型核心)
 * ========================================
 */

PtrHashMap *
ptr_hashmap_create(Bump *arena, size_t initial_capacity)
{
  assert(arena != NULL && "Bump arena cannot be NULL");
  // 调用泛型的 next_pow2
  size_t num_buckets = ptr_hashmap_get_min_buckets_for_entries(initial_capacity);

  PtrHashMap *map = BUMP_ALLOC(arena, PtrHashMap);
  if (!map)
    return NULL;

  PtrHashMapBucket *buckets = BUMP_ALLOC_SLICE_ZEROED(arena, PtrHashMapBucket, num_buckets);
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
ptr_hashmap_get(const PtrHashMap *map, void *key)
{
  PtrHashMapBucket *bucket;
  // 调用泛型的 find_bucket
  if (ptr_hashmap_find_bucket(map, key, &bucket))
  {
    return bucket->value;
  }
  return NULL;
}

bool
ptr_hashmap_remove(PtrHashMap *map, void *key)
{
  PtrHashMapBucket *bucket;
  // 调用泛型的 find_bucket
  if (ptr_hashmap_find_bucket(map, key, &bucket))
  {
    bucket->key = ptr_hashmap_get_tombstone_key(); // 特化的哨兵
    bucket->value = NULL;
    map->num_entries--;
    map->num_tombstones++;
    return true;
  }
  return false;
}

bool
ptr_hashmap_put(PtrHashMap *map, void *key, void *value)
{
  assert(key != NULL && "Key cannot be NULL (reserved for Empty)");
  assert(key != (void *)-1 && "Key cannot be -1 (reserved for Tombstone)");

  PtrHashMapBucket *bucket;
  // 调用泛型的 find_bucket
  bool found = ptr_hashmap_find_bucket(map, key, &bucket);

  if (found)
  {
    bucket->value = value;
    return true;
  }

  assert(bucket != NULL && "find_bucket must return a valid slot");

  size_t total_load = map->num_entries + map->num_tombstones + 1;
  if (total_load * 4 >= map->num_buckets * 3)
  {
    // 调用泛型的 grow
    if (!ptr_hashmap_grow(map))
    {
      return false; // OOM on grow
    }
    found = ptr_hashmap_find_bucket(map, key, &bucket); // 重新 find
    assert(!found && "Key should not exist after grow");
    assert(bucket != NULL);
  }

  if (ptr_hashmap_key_is_tombstone(bucket->key))
  {
    map->num_tombstones--;
  }

  // --- 这是 `ptr` 的特化逻辑 ---
  // Key 被直接存储
  bucket->key = key;
  bucket->value = value;
  map->num_entries++;

  return true;
}

bool
ptr_hashmap_contains(const PtrHashMap *map, void *key)
{
  PtrHashMapBucket *bucket;
  return ptr_hashmap_find_bucket(map, key, &bucket);
}

size_t
ptr_hashmap_size(const PtrHashMap *map)
{
  return map->num_entries;
}
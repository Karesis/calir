#include "utils/hashmap/ptr.h"
#include "utils/bump.h"
#include <assert.h>
#include <string.h> // for memcmp, memcpy

// 1. 包含 xxhash.h 并内联实现
#define XXH_INLINE_ALL
#include "utils/xxhash.h"

/*
 * --- 内部 Key/Bucket 结构 ---
 */

// Key 就是 void*

// 哈希表桶 (Bucket)
typedef struct
{
  void *key;
  void *value;
} PtrHashMapBucket;

// PtrHashMap 结构体的完整定义
struct PtrHashMap_t
{
  Bump *arena; // 用于所有分配
  PtrHashMapBucket *buckets;
  size_t num_entries;
  size_t num_tombstones;
  size_t num_buckets; // 必须始终是 2 的幂
};

/*
 * --- 哨兵键 (Sentinel Keys) ---
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

/*
 * --- 内部 Key "Trait" 帮助函数 ---
 */

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
  // Hash 指针 *本身* 的值
  return XXH3_64bits(&key, sizeof(void *));
}

/*
 * --- 内部核心逻辑 ---
 */

static inline size_t
ptr_hashmap_next_pow2(size_t n)
{
  if (n <= 2)
    return 2;
  n--;
  n |= n >> 1;
  n |= n >> 2;
  n |= n >> 4;
  n |= n >> 8;
  n |= n >> 16;
  if (sizeof(size_t) == 8)
    n |= n >> 32;
  n++;
  return n;
}

static size_t
ptr_hashmap_get_min_buckets_for_entries(size_t num_entries)
{
  if (num_entries == 0)
    return 2;
  size_t required = (num_entries * 4 / 3) + 1;
  return ptr_hashmap_next_pow2(required);
}

static bool
ptr_hashmap_find_bucket(const PtrHashMap *map, void *key, PtrHashMapBucket **found_bucket)
{
  *found_bucket = NULL;
  if (map->num_buckets == 0)
  {
    return false;
  }

  uint64_t hash = ptr_hashmap_get_hash(key);
  size_t bucket_mask = map->num_buckets - 1;
  size_t bucket_idx = (size_t)(hash & bucket_mask);
  size_t probe_amt = 1;

  PtrHashMapBucket *first_tombstone = NULL;

  while (true)
  {
    PtrHashMapBucket *bucket = &map->buckets[bucket_idx];

    if (ptr_hashmap_key_is_equal(bucket->key, key))
    {
      *found_bucket = bucket;
      return true;
    }
    if (ptr_hashmap_key_is_empty(bucket->key))
    {
      *found_bucket = (first_tombstone != NULL) ? first_tombstone : bucket;
      return false;
    }
    if (ptr_hashmap_key_is_tombstone(bucket->key))
    {
      if (first_tombstone == NULL)
      {
        first_tombstone = bucket;
      }
    }
    bucket_idx = (bucket_idx + probe_amt++) & bucket_mask;
  }
}

static bool
ptr_hashmap_grow(PtrHashMap *map)
{
  size_t old_num_buckets = map->num_buckets;
  PtrHashMapBucket *old_buckets = map->buckets;
  size_t new_num_buckets = ptr_hashmap_get_min_buckets_for_entries(map->num_entries * 2);

  PtrHashMapBucket *new_buckets = BUMP_ALLOC_SLICE_ZEROED(map->arena, PtrHashMapBucket, new_num_buckets);
  if (!new_buckets)
    return false; // OOM

  map->buckets = new_buckets;
  map->num_buckets = new_num_buckets;
  map->num_entries = 0;
  map->num_tombstones = 0;

  for (size_t i = 0; i < old_num_buckets; i++)
  {
    PtrHashMapBucket *old_bucket = &old_buckets[i];
    if (!ptr_hashmap_key_is_sentinel(old_bucket->key))
    {
      PtrHashMapBucket *dest_bucket;
      bool found = ptr_hashmap_find_bucket(map, old_bucket->key, &dest_bucket);
      (void)found;
      assert(!found && "Re-hashing should never find the key");
      assert(dest_bucket != NULL && "Re-hashing must find a slot");

      dest_bucket->key = old_bucket->key;
      dest_bucket->value = old_bucket->value;
      map->num_entries++;
    }
  }
  return true;
}

/*
 * ========================================
 * --- 公共 API 实现 ---
 * ========================================
 */

PtrHashMap *
ptr_hashmap_create(Bump *arena, size_t initial_capacity)
{
  assert(arena != NULL && "Bump arena cannot be NULL");
  size_t num_buckets = ptr_hashmap_get_min_buckets_for_entries(initial_capacity);

  PtrHashMap *map = BUMP_ALLOC(arena, PtrHashMap);
  if (!map)
    return NULL;

  PtrHashMapBucket *buckets = BUMP_ALLOC_SLICE_ZEROED(map->arena, PtrHashMapBucket, num_buckets);
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
  if (ptr_hashmap_find_bucket(map, key, &bucket))
  {
    return bucket->value;
  }
  return NULL;
}

bool
ptr_hashmap_contains(const PtrHashMap *map, void *key)
{
  PtrHashMapBucket *bucket;
  return ptr_hashmap_find_bucket(map, key, &bucket);
}

bool
ptr_hashmap_remove(PtrHashMap *map, void *key)
{
  PtrHashMapBucket *bucket;
  if (ptr_hashmap_find_bucket(map, key, &bucket))
  {
    bucket->key = ptr_hashmap_get_tombstone_key();
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
  // Key 不能是我们的哨兵值
  assert(key != NULL && "Key cannot be NULL (reserved for Empty)");
  assert(key != (void *)-1 && "Key cannot be -1 (reserved for Tombstone)");

  PtrHashMapBucket *bucket;
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
    if (!ptr_hashmap_grow(map))
    {
      return false; // OOM on grow
    }
    found = ptr_hashmap_find_bucket(map, key, &bucket);
    assert(!found && "Key should not exist after grow");
    assert(bucket != NULL);
  }

  if (ptr_hashmap_key_is_tombstone(bucket->key))
  {
    map->num_tombstones--;
  }

  // 直接存储指针, 不做复制
  bucket->key = key;
  bucket->value = value;
  map->num_entries++;

  return true;
}

size_t
ptr_hashmap_size(const PtrHashMap *map)
{
  return map->num_entries;
}
#include "utils/hashmap/str_slice.h"
#include "utils/bump.h"
#include <assert.h>
#include <string.h> // for memcmp, memcpy

// 1. 包含 xxhash.h 并内联实现
#define XXH_INLINE_ALL
#include "utils/xxhash.h"

/*
 * --- 内部 Key/Bucket 结构 ---
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
struct StrHashMap_t
{
  Bump *arena; // 用于所有分配
  StrHashMapBucket *buckets;
  size_t num_entries;
  size_t num_tombstones;
  size_t num_buckets; // 必须始终是 2 的幂
};

/*
 * --- 哨兵键 (Sentinel Keys) ---
 */

static StrSlice
str_hashmap_get_empty_key(void)
{
  return (StrSlice){.body = NULL, .len = 0};
}

static StrSlice
str_hashmap_get_tombstone_key(void)
{
  return (StrSlice){.body = (const char *)-1, .len = (size_t)-1};
}

/*
 * --- 内部 Key "Trait" 帮助函数 ---
 */

static inline bool
str_hashmap_key_is_equal(StrSlice k1, StrSlice k2)
{
  if (k1.body == k2.body)
    return true;
  if (k1.body == NULL || k1.body == (const char *)-1)
    return false;
  if (k2.body == NULL || k2.body == (const char *)-1)
    return false;
  return (k1.len == k2.len) && (memcmp(k1.body, k2.body, k1.len) == 0);
}

static inline bool
str_hashmap_key_is_empty(StrSlice k)
{
  return k.body == NULL;
}

static inline bool
str_hashmap_key_is_tombstone(StrSlice k)
{
  return k.body == (const char *)-1;
}

static inline bool
str_hashmap_key_is_sentinel(StrSlice k)
{
  return k.body == NULL || k.body == (const char *)-1;
}

static inline uint64_t
str_hashmap_get_hash(StrSlice key)
{
  return XXH3_64bits(key.body, key.len);
}

/*
 * --- 内部核心逻辑 ---
 */

static inline size_t
str_hashmap_next_pow2(size_t n)
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
str_hashmap_get_min_buckets_for_entries(size_t num_entries)
{
  if (num_entries == 0)
    return 2;
  size_t required = (num_entries * 4 / 3) + 1;
  return str_hashmap_next_pow2(required);
}

static bool
str_hashmap_find_bucket(const StrHashMap *map, StrSlice key, StrHashMapBucket **found_bucket)
{
  *found_bucket = NULL;
  if (map->num_buckets == 0)
  {
    return false;
  }

  uint64_t hash = str_hashmap_get_hash(key);
  size_t bucket_mask = map->num_buckets - 1;
  size_t bucket_idx = (size_t)(hash & bucket_mask);
  size_t probe_amt = 1;

  StrHashMapBucket *first_tombstone = NULL;

  while (true)
  {
    StrHashMapBucket *bucket = &map->buckets[bucket_idx];

    if (str_hashmap_key_is_equal(bucket->key, key))
    {
      *found_bucket = bucket;
      return true;
    }
    if (str_hashmap_key_is_empty(bucket->key))
    {
      *found_bucket = (first_tombstone != NULL) ? first_tombstone : bucket;
      return false;
    }
    if (str_hashmap_key_is_tombstone(bucket->key))
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
str_hashmap_grow(StrHashMap *map)
{
  size_t old_num_buckets = map->num_buckets;
  StrHashMapBucket *old_buckets = map->buckets;
  size_t new_num_buckets = str_hashmap_get_min_buckets_for_entries(map->num_entries * 2);

  StrHashMapBucket *new_buckets = BUMP_ALLOC_SLICE_ZEROED(map->arena, StrHashMapBucket, new_num_buckets);
  if (!new_buckets)
    return false; // OOM

  map->buckets = new_buckets;
  map->num_buckets = new_num_buckets;
  map->num_entries = 0;
  map->num_tombstones = 0;

  for (size_t i = 0; i < old_num_buckets; i++)
  {
    StrHashMapBucket *old_bucket = &old_buckets[i];
    if (!str_hashmap_key_is_sentinel(old_bucket->key))
    {
      StrHashMapBucket *dest_bucket;
      bool found = str_hashmap_find_bucket(map, old_bucket->key, &dest_bucket);
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

StrHashMap *
str_hashmap_create(Bump *arena, size_t initial_capacity)
{
  assert(arena != NULL && "Bump arena cannot be NULL");
  size_t num_buckets = str_hashmap_get_min_buckets_for_entries(initial_capacity);

  StrHashMap *map = BUMP_ALLOC(arena, StrHashMap);
  if (!map)
    return NULL;

  StrHashMapBucket *buckets = BUMP_ALLOC_SLICE_ZEROED(arena, StrHashMapBucket, num_buckets);
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
str_hashmap_get(const StrHashMap *map, const char *key_body, size_t key_len)
{
  StrSlice key_to_find = {.body = key_body, .len = key_len};
  StrHashMapBucket *bucket;
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
    bucket->key = str_hashmap_get_tombstone_key();
    bucket->value = NULL;
    map->num_entries--;
    map->num_tombstones++;
    return true;
  }
  return false;
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

  if (str_hashmap_key_is_tombstone(bucket->key))
  {
    map->num_tombstones--;
  }

  // **关键**: 复制 Key 到 Arena 中
  const char *new_key_body = bump_alloc_copy(map->arena, key_body, key_len, 1);
  if (!new_key_body && key_len > 0)
  {
    return false; // OOM on key copy
  }

  bucket->key.body = new_key_body;
  bucket->key.len = key_len;
  bucket->value = value;
  map->num_entries++;

  return true;
}

size_t
str_hashmap_size(const StrHashMap *map)
{
  return map->num_entries;
}
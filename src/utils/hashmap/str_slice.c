/* src/hashmap/str_slice.c */
#include "utils/hashmap/str_slice.h"
#include "utils/bump.h"
#include <assert.h>
#include <string.h> // for memcmp, memcpy

// 1. 包含 xxhash.h 并内联实现
#define XXH_INLINE_ALL
#include "utils/xxhash.h"

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
  size_t num_entries;
  size_t num_tombstones;
  size_t num_buckets; // 必须始终是 2 的幂
};

/*
 * ========================================
 * --- 2. "Trait" 函数实现 ---
 * ========================================
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

static inline bool
str_hashmap_key_is_equal(StrSlice k1, StrSlice k2)
{
  if (k1.body == k2.body)
    return true;
  // 哨兵值不参与 memcmp
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
 * ========================================
 * --- 3. 包含泛型核心实现 ---
 * ========================================
 */

// 定义模板参数
#define CHM_PREFIX str
#define CHM_K_TYPE StrSlice
#define CHM_V_TYPE void *
#define CHM_API_TYPE StrHashMap
#define CHM_STRUCT_TYPE StrHashMap_t
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
  // 调用泛型的 get_min_buckets_for_entries
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
  // API 特化: 将 (char*, len) 转换为内部 StrSlice
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

  // 调用泛型的 find_bucket
  if (str_hashmap_find_bucket(map, key_to_find, &bucket))
  {
    bucket->key = str_hashmap_get_tombstone_key(); // 特化的哨兵
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
  // API 特化: 将 (char*, len) 转换为内部 StrSlice
  StrSlice key_to_find = {.body = key_body, .len = key_len};
  StrHashMapBucket *bucket;

  // 调用泛型的 find_bucket
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
    // 调用泛型的 grow
    if (!str_hashmap_grow(map))
    {
      return false; // OOM on grow
    }
    // 重新 find
    found = str_hashmap_find_bucket(map, key_to_find, &bucket);
    assert(!found && "Key should not exist after grow");
    assert(bucket != NULL);
  }

  if (str_hashmap_key_is_tombstone(bucket->key))
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
  map->num_entries++;

  return true;
}

size_t
str_hashmap_size(const StrHashMap *map)
{
  return map->num_entries;
}
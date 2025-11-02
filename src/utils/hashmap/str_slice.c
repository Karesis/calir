/*
 * Copyright 2025 Karesis
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/* utils/hashmap/str_slice.c */
#include "utils/hashmap/str_slice.h"
#include "utils/bump.h"
#include <assert.h>
#include <string.h>

#define XXH_INLINE_ALL
#include "utils/xxhash.h"

#include "utils/hashmap/common.h"

/*
 * ========================================
 * --- 1. 类型和结构体定义 ---
 * ========================================
 */

typedef struct
{
  const char *body;
  size_t len;
} StrSlice;

typedef struct
{
  StrSlice key;
  void *value;
} StrHashMapBucket;

struct StrHashMap
{
  Bump *arena;
  StrHashMapBucket *buckets;
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

#define CHM_PREFIX str
#define CHM_K_TYPE StrSlice
#define CHM_V_TYPE void *
#define CHM_API_TYPE StrHashMap
#define CHM_STRUCT_TYPE StrHashMap
#define CHM_BUCKET_TYPE StrHashMapBucket

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

  StrHashMapBucket *buckets = BUMP_ALLOC_SLICE(arena, StrHashMapBucket, num_buckets);
  if (!buckets)
    return NULL;

  uint8_t *states = BUMP_ALLOC_SLICE_ZEROED(arena, uint8_t, num_buckets);
  if (!states)
    return NULL;

  map->arena = arena;
  map->buckets = buckets;
  map->states = states;
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
      return false;
    }
    found = str_hashmap_find_bucket(map, key_to_find, &bucket);
    assert(!found && "Key should not exist after grow");
    assert(bucket != NULL);
  }

  size_t bucket_idx = (size_t)(bucket - map->buckets);

  if (map->states[bucket_idx] == BUCKET_TOMBSTONE)
  {
    map->num_tombstones--;
  }

  const char *new_key_body = bump_alloc_copy(map->arena, key_body, key_len, 1);
  if (!new_key_body && key_len > 0)
  {
    return false;
  }

  bucket->key.body = new_key_body;
  bucket->key.len = key_len;
  bucket->value = value;
  map->states[bucket_idx] = BUCKET_FILLED;
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
      return false;
    }
    found = str_hashmap_find_bucket(map, key_to_find, &bucket);
    assert(!found && "Key should not exist after grow");
    assert(bucket != NULL);
  }

  size_t bucket_idx = (size_t)(bucket - map->buckets);

  if (map->states[bucket_idx] == BUCKET_TOMBSTONE)
  {
    map->num_tombstones--;
  }

  bucket->key.body = key_body;
  bucket->key.len = key_len;
  bucket->value = value;
  map->states[bucket_idx] = BUCKET_FILLED;
  map->num_entries++;

  return true;
}

size_t
str_hashmap_size(const StrHashMap *map)
{
  return map->num_entries;
}

/*
 * ========================================
 * --- 5. 迭代器 API 实现 ---
 * ========================================
 */

#define _CHM_PASTE3(a, b, c) a##b##c
#define CHM_PASTE3(a, b, c) _CHM_PASTE3(a, b, c)
#define CHM_FUNC(prefix, suffix) CHM_PASTE3(prefix, _hashmap_, suffix)

#define CHM_PREFIX str
#define CHM_API_TYPE StrHashMap
#define CHM_STRUCT_TYPE StrHashMap
#define CHM_BUCKET_TYPE StrHashMapBucket
#define CHM_ENTRY_TYPE StrHashMapEntry
#define CHM_ITER_TYPE StrHashMapIter

#define CHM_ITER_ASSIGN_ENTRY(entry_out, bucket)                                                                       \
  do                                                                                                                   \
  {                                                                                                                    \
    (entry_out)->key_body = (bucket)->key.body;                                                                        \
    (entry_out)->key_len = (bucket)->key.len;                                                                          \
    (entry_out)->value = (bucket)->value;                                                                              \
  } while (0)

#include "utils/hashmap/iterator.inc"

#undef CHM_ITER_ASSIGN_ENTRY

#undef _CHM_PASTE3
#undef CHM_PASTE3
#undef CHM_FUNC
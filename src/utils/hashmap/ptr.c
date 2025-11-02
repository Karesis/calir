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

/* utils/hashmap/ptr.c */
#include "utils/hashmap/ptr.h"
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
  void *key;
  void *value;
} PtrHashMapBucket;

struct PtrHashMap
{
  Bump *arena;
  PtrHashMapBucket *buckets;
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
ptr_hashmap_key_is_equal(void *k1, void *k2)
{
  return k1 == k2;
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

#define CHM_PREFIX ptr
#define CHM_K_TYPE void *
#define CHM_V_TYPE void *
#define CHM_API_TYPE PtrHashMap
#define CHM_STRUCT_TYPE PtrHashMap
#define CHM_BUCKET_TYPE PtrHashMapBucket

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
  size_t num_buckets = ptr_hashmap_get_min_buckets_for_entries(initial_capacity);

  PtrHashMap *map = BUMP_ALLOC(arena, PtrHashMap);
  if (!map)
    return NULL;

  PtrHashMapBucket *buckets = BUMP_ALLOC_SLICE(arena, PtrHashMapBucket, num_buckets);
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
ptr_hashmap_remove(PtrHashMap *map, void *key)
{
  PtrHashMapBucket *bucket;
  if (ptr_hashmap_find_bucket(map, key, &bucket))
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
ptr_hashmap_put(PtrHashMap *map, void *key, void *value)
{
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
      return false;
    }

    found = ptr_hashmap_find_bucket(map, key, &bucket);
    assert(!found && "Key should not exist after grow");
    assert(bucket != NULL);
  }

  size_t bucket_idx = (size_t)(bucket - map->buckets);

  if (map->states[bucket_idx] == BUCKET_TOMBSTONE)
  {
    map->num_tombstones--;
  }

  bucket->key = key;
  bucket->value = value;
  map->states[bucket_idx] = BUCKET_FILLED;
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

/*
 * ========================================
 * --- 5. 迭代器 API 实现 ---
 * ========================================
 */

#define _CHM_PASTE3(a, b, c) a##b##c
#define CHM_PASTE3(a, b, c) _CHM_PASTE3(a, b, c)
#define CHM_FUNC(prefix, suffix) CHM_PASTE3(prefix, _hashmap_, suffix)

#define CHM_PREFIX ptr
#define CHM_API_TYPE PtrHashMap
#define CHM_STRUCT_TYPE PtrHashMap
#define CHM_BUCKET_TYPE PtrHashMapBucket
#define CHM_ENTRY_TYPE PtrHashMapEntry
#define CHM_ITER_TYPE PtrHashMapIter

#include "utils/hashmap/iterator.inc"

#undef _CHM_PASTE3
#undef CHM_PASTE3
#undef CHM_FUNC
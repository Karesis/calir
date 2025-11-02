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


/* src/utils/hashmap/generic.c */
#include "utils/hashmap/generic.h"
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
  const void *key;
  void *value;
} GenericHashMapBucket;


struct GenericHashMap
{
  Bump *arena;
  GenericHashMapBucket *buckets;
  uint8_t *states;
  size_t num_entries;
  size_t num_tombstones;
  size_t num_buckets;


  GenericHashFn hash_fn;
  GenericEqualFn equal_fn;
};

/*
 * ========================================
 * --- 2. "Trait" 函数实现 (通过宏重定义) ---
 * ========================================
 */






/*
 * ========================================
 * --- 3. 包含泛型核心实现 ---
 * ========================================
 */


#define CHM_PREFIX generic
#define CHM_K_TYPE const void *
#define CHM_V_TYPE void *
#define CHM_API_TYPE GenericHashMap
#define CHM_STRUCT_TYPE GenericHashMap
#define CHM_BUCKET_TYPE GenericHashMapBucket





#define CHM_HASH_FUNC map->hash_fn





#define CHM_TRAIT(suffix) map->equal_fn







#include "utils/hashmap/core.inc"

/*
 * ========================================
 * --- 4. 公共 API 实现 ---
 * ========================================
 */

GenericHashMap *
generic_hashmap_create(Bump *arena, size_t initial_capacity, GenericHashFn hash_fn, GenericEqualFn equal_fn)
{
  assert(arena != NULL && "Bump arena cannot be NULL");
  assert(hash_fn != NULL && "Hash function cannot be NULL");
  assert(equal_fn != NULL && "Equal function cannot be NULL");

  size_t num_buckets = generic_hashmap_get_min_buckets_for_entries(initial_capacity);

  GenericHashMap *map = BUMP_ALLOC(arena, GenericHashMap);
  if (!map)
    return NULL;


  GenericHashMapBucket *buckets = BUMP_ALLOC_SLICE(arena, GenericHashMapBucket, num_buckets);
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
  map->hash_fn = hash_fn;
  map->equal_fn = equal_fn;

  return map;
}

void *
generic_hashmap_get(const GenericHashMap *map, const void *key)
{
  GenericHashMapBucket *bucket;

  if (generic_hashmap_find_bucket(map, key, &bucket))
  {
    return bucket->value;
  }
  return NULL;
}

bool
generic_hashmap_remove(GenericHashMap *map, const void *key)
{
  GenericHashMapBucket *bucket;
  if (generic_hashmap_find_bucket(map, key, &bucket))
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
generic_hashmap_put(GenericHashMap *map, const void *key, void *value)
{
  GenericHashMapBucket *bucket;
  bool found = generic_hashmap_find_bucket(map, key, &bucket);

  if (found)
  {

    bucket->value = value;
    return true;
  }


  assert(bucket != NULL && "find_bucket must return a valid slot");


  size_t total_load = map->num_entries + map->num_tombstones + 1;
  if (total_load * 4 >= map->num_buckets * 3)
  {
    if (!generic_hashmap_grow(map))
    {
      return false;
    }

    found = generic_hashmap_find_bucket(map, key, &bucket);
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
generic_hashmap_contains(const GenericHashMap *map, const void *key)
{
  GenericHashMapBucket *bucket;
  return generic_hashmap_find_bucket(map, key, &bucket);
}

size_t
generic_hashmap_size(const GenericHashMap *map)
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


#define CHM_PREFIX generic
#define CHM_API_TYPE GenericHashMap
#define CHM_STRUCT_TYPE GenericHashMap
#define CHM_BUCKET_TYPE GenericHashMapBucket
#define CHM_ENTRY_TYPE GenericHashMapEntry
#define CHM_ITER_TYPE GenericHashMapIter



#include "utils/hashmap/iterator.inc"


#undef _CHM_PASTE3
#undef CHM_PASTE3
#undef CHM_FUNC
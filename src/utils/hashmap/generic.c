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

// 包含我们重构后的状态定义
#include "utils/hashmap/common.h"

/*
 * ========================================
 * --- 1. 类型和结构体定义 ---
 * ========================================
 */

typedef struct
{
  const void *key; // Key 是一个指向用户 struct 的指针
  void *value;
} GenericHashMapBucket;

// GenericHashMap 结构体的完整定义
struct GenericHashMap
{
  Bump *arena;
  GenericHashMapBucket *buckets;
  uint8_t *states;
  size_t num_entries;
  size_t num_tombstones;
  size_t num_buckets;

  // --- 泛型特定的字段 ---
  GenericHashFn hash_fn;
  GenericEqualFn equal_fn;
};

/*
 * ========================================
 * --- 2. "Trait" 函数实现 (通过宏重定义) ---
 * ========================================
 */

// 'GenericHashMap' 不需要 "Trait" 函数。
// 我们将在包含 'core.inc' 之前，
// 直接将 'CHM_HASH_FUNC' 和 'CHM_TRAIT(is_equal)' 宏
// 定义为调用 'map' 上的函数指针。

/*
 * ========================================
 * --- 3. 包含泛型核心实现 ---
 * ========================================
 */

// 定义模板参数
#define CHM_PREFIX generic
#define CHM_K_TYPE const void * // Key 的类型是 void*
#define CHM_V_TYPE void *
#define CHM_API_TYPE GenericHashMap
#define CHM_STRUCT_TYPE GenericHashMap
#define CHM_BUCKET_TYPE GenericHashMapBucket

// 'core.inc' 调用: CHM_HASH_FUNC(key)
// 我们将 CHM_HASH_FUNC 定义为 'map->hash_fn' (对象宏)
// 预处理器展开: map->hash_fn(key)
// (因为 'core.inc' 中的 #ifndef CHM_HASH_FUNC, 这个定义会生效)
#define CHM_HASH_FUNC map->hash_fn

// 'core.inc' 调用: CHM_TRAIT(is_equal)(k1, k2)
// 我们将 CHM_TRAIT(suffix) 定义为 'map->equal_fn' (函数式宏)
// 预处理器展开: map->equal_fn(k1, k2)
// (因为 'core.inc' 中的 #ifndef CHM_TRAIT, 这个定义会生效)
#define CHM_TRAIT(suffix) map->equal_fn

// 实例化泛型函数
// 这将定义:
// - generic_hashmap_next_pow2
// - generic_hashmap_get_min_buckets_for_entries
// - generic_hashmap_find_bucket (!! 使用 map->hash_fn 和 map->equal_fn)
// - generic_hashmap_grow
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

  // Buckets 不需要清零
  GenericHashMapBucket *buckets = BUMP_ALLOC_SLICE(arena, GenericHashMapBucket, num_buckets);
  if (!buckets)
    return NULL;

  // States *必须* 清零
  uint8_t *states = BUMP_ALLOC_SLICE_ZEROED(arena, uint8_t, num_buckets);
  if (!states)
    return NULL; // OOM

  map->arena = arena;
  map->buckets = buckets;
  map->states = states;
  map->num_entries = 0;
  map->num_tombstones = 0;
  map->num_buckets = num_buckets;
  map->hash_fn = hash_fn;   // <-- 存储函数指针
  map->equal_fn = equal_fn; // <-- 存储函数指针

  return map;
}

void *
generic_hashmap_get(const GenericHashMap *map, const void *key)
{
  GenericHashMapBucket *bucket;
  // 调用泛型的 find_bucket (它将使用函数指针)
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
    // Key 存在, 将其槽位标记为墓碑
    size_t bucket_idx = (size_t)(bucket - map->buckets);
    map->states[bucket_idx] = BUCKET_TOMBSTONE;

    bucket->value = NULL;
    map->num_entries--;
    map->num_tombstones++;
    return true;
  }
  return false; // Key 不存在
}

bool
generic_hashmap_put(GenericHashMap *map, const void *key, void *value)
{
  GenericHashMapBucket *bucket;
  bool found = generic_hashmap_find_bucket(map, key, &bucket);

  if (found)
  {
    // Key 已存在, 更新 value
    bucket->value = value;
    return true;
  }

  // Key 不存在, 'bucket' 指向我们应该插入的槽位
  assert(bucket != NULL && "find_bucket must return a valid slot");

  // 检查是否需要扩容
  size_t total_load = map->num_entries + map->num_tombstones + 1;
  if (total_load * 4 >= map->num_buckets * 3)
  {
    if (!generic_hashmap_grow(map)) // grow() 已被重构
    {
      return false; // OOM on grow
    }
    // 扩容后, 必须重新查找槽位
    found = generic_hashmap_find_bucket(map, key, &bucket);
    assert(!found && "Key should not exist after grow");
    assert(bucket != NULL);
  }

  size_t bucket_idx = (size_t)(bucket - map->buckets);

  // 如果我们复用了墓碑, 减少墓碑计数
  if (map->states[bucket_idx] == BUCKET_TOMBSTONE)
  {
    map->num_tombstones--;
  }

  // 插入新条目 (Key 是指针, 直接存储)
  bucket->key = key;
  bucket->value = value;
  map->states[bucket_idx] = BUCKET_FILLED; // <-- 标记为 FILLED
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

// 1. (FIX) 为 CHM_FUNC 定义粘贴宏
#define _CHM_PASTE3(a, b, c) a##b##c
#define CHM_PASTE3(a, b, c) _CHM_PASTE3(a, b, c)
#define CHM_FUNC(prefix, suffix) CHM_PASTE3(prefix, _hashmap_, suffix)

// 2. 为 iterator.inc 设置 "模板参数"
#define CHM_PREFIX generic
#define CHM_API_TYPE GenericHashMap
#define CHM_STRUCT_TYPE GenericHashMap
#define CHM_BUCKET_TYPE GenericHashMapBucket
#define CHM_ENTRY_TYPE GenericHashMapEntry // 来自 generic.h
#define CHM_ITER_TYPE GenericHashMapIter   // 来自 generic.h

// 3. 包含通用实现
//    (现在 CHM_FUNC 已经被正确定义了)
#include "utils/hashmap/iterator.inc"

// 4. 清理本节定义的宏
#undef _CHM_PASTE3
#undef CHM_PASTE3
#undef CHM_FUNC
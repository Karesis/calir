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
#include <string.h> // for memcmp, memcpy

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

// PtrHashMap 结构体的完整定义
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

// 定义模板参数
#define CHM_PREFIX ptr
#define CHM_K_TYPE void *
#define CHM_V_TYPE void *
#define CHM_API_TYPE PtrHashMap
#define CHM_STRUCT_TYPE PtrHashMap
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
  size_t num_buckets = ptr_hashmap_get_min_buckets_for_entries(initial_capacity);

  PtrHashMap *map = BUMP_ALLOC(arena, PtrHashMap);
  if (!map)
    return NULL;

  // Buckets 不需要清零, 状态由 'states' 数组管理
  PtrHashMapBucket *buckets = BUMP_ALLOC_SLICE(arena, PtrHashMapBucket, num_buckets);
  if (!buckets)
    return NULL;

  // States *必须* 清零 (BUMP_ALLOC_SLICE_ZEROED)
  // 因为 BUCKET_EMPTY == 0
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
  if (ptr_hashmap_find_bucket(map, key, &bucket))
  {
    // Key 存在, 将其槽位标记为墓碑
    size_t bucket_idx = (size_t)(bucket - map->buckets);
    map->states[bucket_idx] = BUCKET_TOMBSTONE;

    // (不再需要设置 bucket->key = TOMBSTONE_K)
    bucket->value = NULL; // (良好实践)
    map->num_entries--;
    map->num_tombstones++;
    return true;
  }
  return false; // Key 不存在
}

bool
ptr_hashmap_put(PtrHashMap *map, void *key, void *value)
{
  PtrHashMapBucket *bucket;
  bool found = ptr_hashmap_find_bucket(map, key, &bucket);

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
    if (!ptr_hashmap_grow(map)) // grow() 已被重构为使用 states
    {
      return false; // OOM on grow
    }
    // 扩容后, 必须重新查找槽位
    found = ptr_hashmap_find_bucket(map, key, &bucket);
    assert(!found && "Key should not exist after grow");
    assert(bucket != NULL);
  }

  size_t bucket_idx = (size_t)(bucket - map->buckets);

  // 如果我们复用了墓碑, 减少墓碑计数
  if (map->states[bucket_idx] == BUCKET_TOMBSTONE)
  {
    map->num_tombstones--;
  }

  // 插入新条目
  bucket->key = key;
  bucket->value = value;
  map->states[bucket_idx] = BUCKET_FILLED; // <-- 标记为 FILLED
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

// 1. (FIX) 为 CHM_FUNC 定义粘贴宏
#define _CHM_PASTE3(a, b, c) a##b##c
#define CHM_PASTE3(a, b, c) _CHM_PASTE3(a, b, c)
#define CHM_FUNC(prefix, suffix) CHM_PASTE3(prefix, _hashmap_, suffix)

// 为 iterator.inc 设置 "模板参数"
#define CHM_PREFIX ptr
#define CHM_API_TYPE PtrHashMap
#define CHM_STRUCT_TYPE PtrHashMap // 在 .c 文件中, PtrHashMap 是完整类型
#define CHM_BUCKET_TYPE PtrHashMapBucket
#define CHM_ENTRY_TYPE PtrHashMapEntry // 来自 ptr.h
#define CHM_ITER_TYPE PtrHashMapIter   // 来自 ptr.h

// (我们 *不* 定义 CHM_ITER_ASSIGN_ENTRY, 所以它会使用默认实现)

// 包含通用实现
#include "utils/hashmap/iterator.inc"

#undef _CHM_PASTE3
#undef CHM_PASTE3
#undef CHM_FUNC
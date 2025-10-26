/*
 * utils/hashmap/core.inc
 *
 * 泛型哈希表核心实现模板。
 * * 在包含此文件之前，必须定义以下宏:
 *
 * - CHM_PREFIX:      函数名的前缀 (例如: ptr, str)
 * - CHM_K_TYPE:      Key 的类型 (例如: void*, StrSlice)
 * - CHM_V_TYPE:      Value 的类型 (例如: void*)
 * - CHM_API_TYPE:    公开的 Map 类型 (例如: PtrHashMap)
 * - CHM_STRUCT_TYPE: 内部的 Map 结构体类型 (例如: PtrHashMap_t)
 * - CHM_BUCKET_TYPE: 内部的 Bucket 结构体类型 (例如: PtrHashMapBucket)
 *
 * * 包含方必须提供以下 "Trait" 函数:
 *
 * - CHM_TRAIT(is_equal) -> [prefix]_hashmap_key_is_equal(k1, k2)
 * - CHM_HASH_FUNC       -> [prefix]_hashmap_get_hash(key)
 *
 * * 包含方的结构体 (CHM_STRUCT_TYPE) 必须包含 'uint8_t *states' 成员。
 */

// 确保 BucketState 已被包含
#include "utils/hashmap/common.h"

// --- 宏工具 ---
#define _CHM_PASTE(a, b) a##b
#define CHM_PASTE(a, b) _CHM_PASTE(a, b)
#define _CHM_PASTE3(a, b, c) a##b##c
#define CHM_PASTE3(a, b, c) _CHM_PASTE3(a, b, c)

// --- 泛型函数命名 ---
// CHM_FUNC(ptr, grow) -> ptr_hashmap_grow
#define CHM_FUNC(prefix, suffix) CHM_PASTE3(prefix, _hashmap_, suffix)

// --- 依赖的 "Trait" 函数 (必须由包含方提供) ---
// CHM_TRAIT(is_equal) -> ptr_hashmap_key_is_equal
#ifndef CHM_TRAIT
#define CHM_TRAIT(suffix) CHM_FUNC(CHM_PREFIX, key_##suffix)
#endif

#ifndef CHM_HASH_FUNC
#define CHM_HASH_FUNC CHM_FUNC(CHM_PREFIX, get_hash)
#endif

/*
 * ========================================
 * --- 内部核心逻辑 (泛型实现) ---
 * ========================================
 */

static inline size_t
CHM_FUNC(CHM_PREFIX, next_pow2)(size_t n)
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
CHM_FUNC(CHM_PREFIX, get_min_buckets_for_entries)(size_t num_entries)
{
  if (num_entries == 0)
    return 2;
  // 保持 0.75 的最大负载因子
  size_t required = (num_entries * 4 / 3) + 1;
  return CHM_FUNC(CHM_PREFIX, next_pow2)(required);
}

/**
 * @brief 查找 Key 对应的桶 (泛型实现)
 *
 * @param map   哈希表
 * @param key   要查找的 Key (类型为 CHM_K_TYPE)
 * @param found_bucket [out] 用于存储找到的桶
 * @return true 如果 Key 被找到, false 如果未找到 (但 *found_bucket 会指向可插入的槽)
 */
static bool
CHM_FUNC(CHM_PREFIX, find_bucket)(const CHM_API_TYPE *map, CHM_K_TYPE key, CHM_BUCKET_TYPE **found_bucket)
{
  *found_bucket = NULL;
  if (map->num_buckets == 0)
  {
    return false;
  }

  uint64_t hash = CHM_HASH_FUNC(key); // 调用: [prefix]_hashmap_get_hash(key)
  size_t bucket_mask = map->num_buckets - 1;
  size_t bucket_idx = (size_t)(hash & bucket_mask);
  size_t probe_amt = 1;

  CHM_BUCKET_TYPE *first_tombstone = NULL;

  while (true)
  {
    uint8_t state = map->states[bucket_idx];
    CHM_BUCKET_TYPE *bucket = &map->buckets[bucket_idx];

    if (state == BUCKET_FILLED)
    {
      // 调用: [prefix]_hashmap_key_is_equal(bucket->key, key)
      if (CHM_TRAIT(is_equal)(bucket->key, key))
      {
        *found_bucket = bucket;
        return true;
      }
    }
    else if (state == BUCKET_EMPTY)
    {
      // 找到了空槽。
      // 如果我们之前遇到了墓碑, 返回墓碑; 否则返回这个空槽。
      *found_bucket = (first_tombstone != NULL) ? first_tombstone : bucket;
      return false;
    }
    else // state == BUCKET_TOMBSTONE
    {
      if (first_tombstone == NULL)
      {
        first_tombstone = bucket;
      }
    }

    // 线性探测 (译注: 原代码注释是二次探测，但实现是线性探测 (i+1))
    // (译注2: 稍等, probe_amt++ 提供了二次探测...
    // 不, (idx + probe_amt++) 仍然是线性探测的变种，只是步长递增。
    // 真正的二次探测是 (idx + probe_amt * probe_amt) or (idx + c1*i + c2*i*i)
    // 你的实现 (idx + i++) & mask 是一种步长为 1, 2, 3, 4... 的探测，
    // 这被称为“二次探测”(Quadratic Probing) 的一种形式，有时也叫“算术探测”。
    // 只要 num_buckets 是 2 的幂, 这是 OK 的。)
    bucket_idx = (bucket_idx + probe_amt++) & bucket_mask;
  }
}

static bool
CHM_FUNC(CHM_PREFIX, grow)(CHM_API_TYPE *map)
{
  size_t old_num_buckets = map->num_buckets;
  CHM_BUCKET_TYPE *old_buckets = map->buckets;
  uint8_t *old_states = map->states; // <-- 保存旧的 states 数组

  size_t new_num_buckets = CHM_FUNC(CHM_PREFIX, get_min_buckets_for_entries)(map->num_entries * 2);

  // 1. 分配新空间
  //    Buckets 不需要清零, 因为 'states' 会控制访问
  CHM_BUCKET_TYPE *new_buckets = BUMP_ALLOC_SLICE(map->arena, CHM_BUCKET_TYPE, new_num_buckets);
  if (!new_buckets)
    return false; // OOM

  //    States *必须* 清零 (BUMP_ALLOC_SLICE_ZEROED)
  //    因为 0 == BUCKET_EMPTY
  uint8_t *new_states = BUMP_ALLOC_SLICE_ZEROED(map->arena, uint8_t, new_num_buckets);
  if (!new_states)
    return false; // OOM

  // 2. 更新 map 引用
  map->buckets = new_buckets;
  map->states = new_states; // <-- 更新 states 指针
  map->num_buckets = new_num_buckets;
  map->num_entries = 0;
  map->num_tombstones = 0;

  // 3. Re-hash 旧条目
  for (size_t i = 0; i < old_num_buckets; i++)
  {
    // 只检查 'states' 数组!
    if (old_states[i] == BUCKET_FILLED)
    {
      CHM_BUCKET_TYPE *old_bucket = &old_buckets[i];
      CHM_BUCKET_TYPE *dest_bucket;

      // 在新表上调用 find_bucket
      bool found = CHM_FUNC(CHM_PREFIX, find_bucket)(map, old_bucket->key, &dest_bucket);
      (void)found;
      assert(!found && "Re-hashing should never find the key");
      assert(dest_bucket != NULL && "Re-hashing must find a slot");

      // 复制数据 (Key 和 Value 都是浅拷贝)
      dest_bucket->key = old_bucket->key;
      dest_bucket->value = old_bucket->value;

      // 更新新 'states' 数组中的状态
      size_t dest_idx = (size_t)(dest_bucket - map->buckets);
      map->states[dest_idx] = BUCKET_FILLED;
      map->num_entries++;
    }
  }
  return true;
}

// 清理宏, 防止污染
#undef CHM_PREFIX
#undef CHM_K_TYPE
#undef CHM_V_TYPE
#undef CHM_API_TYPE
#undef CHM_STRUCT_TYPE
#undef CHM_BUCKET_TYPE
#undef _CHM_PASTE
#undef CHM_PASTE
#undef _CHM_PASTE3
#undef CHM_PASTE3
#undef CHM_FUNC
#undef CHM_TRAIT
#undef CHM_HASH_FUNC
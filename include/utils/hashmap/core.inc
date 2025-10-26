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
 */

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
#define CHM_TRAIT(suffix) CHM_FUNC(CHM_PREFIX, key_##suffix)
#define CHM_HASH_FUNC CHM_FUNC(CHM_PREFIX, get_hash)

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
    CHM_BUCKET_TYPE *bucket = &map->buckets[bucket_idx];

    // 调用: [prefix]_hashmap_key_is_equal(bucket->key, key)
    if (CHM_TRAIT(is_equal)(bucket->key, key))
    {
      *found_bucket = bucket;
      return true;
    }
    // 调用: [prefix]_hashmap_key_is_empty(bucket->key)
    if (CHM_TRAIT(is_empty)(bucket->key))
    {
      *found_bucket = (first_tombstone != NULL) ? first_tombstone : bucket;
      return false;
    }
    // 调用: [prefix]_hashmap_key_is_tombstone(bucket->key)
    if (CHM_TRAIT(is_tombstone)(bucket->key))
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
CHM_FUNC(CHM_PREFIX, grow)(CHM_API_TYPE *map)
{
  size_t old_num_buckets = map->num_buckets;
  CHM_BUCKET_TYPE *old_buckets = map->buckets;
  size_t new_num_buckets = CHM_FUNC(CHM_PREFIX, get_min_buckets_for_entries)(map->num_entries * 2);

  CHM_BUCKET_TYPE *new_buckets = BUMP_ALLOC_SLICE(map->arena, CHM_BUCKET_TYPE, new_num_buckets);
  if (!new_buckets)
    return false; // OOM

  // 手动初始化所有新槽位
  // (调用由 *_template.inc 提供的 get_empty_key 函数)
  const CHM_K_TYPE empty_key = CHM_FUNC(CHM_PREFIX, get_empty_key)();
  for (size_t i = 0; i < new_num_buckets; i++)
  {
    new_buckets[i].key = empty_key;
  }

  map->buckets = new_buckets;
  map->num_buckets = new_num_buckets;
  map->num_entries = 0;
  map->num_tombstones = 0;

  for (size_t i = 0; i < old_num_buckets; i++)
  {
    CHM_BUCKET_TYPE *old_bucket = &old_buckets[i];

    // 调用: [prefix]_hashmap_key_is_sentinel(old_bucket->key)
    if (!CHM_TRAIT(is_sentinel)(old_bucket->key))
    {
      CHM_BUCKET_TYPE *dest_bucket;
      // 注意: 这里调用的是新生成的泛型 find_bucket
      bool found = CHM_FUNC(CHM_PREFIX, find_bucket)(map, old_bucket->key, &dest_bucket);
      (void)found;
      assert(!found && "Re-hashing should never find the key");
      assert(dest_bucket != NULL && "Re-hashing must find a slot");

      // Key 的复制是浅拷贝, 这对于 void* 和 StrSlice (指向 arena) 都适用
      dest_bucket->key = old_bucket->key;
      dest_bucket->value = old_bucket->value;
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
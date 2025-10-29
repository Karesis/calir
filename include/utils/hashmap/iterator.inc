/*
 * include/utils/hashmap/iterator.inc
 *
 * 泛型哈希表 "状态迭代器" 实现模板。
 * (对应方案二, 模仿 LLVM DenseMap)
 *
 * * 在包含此文件之前，必须定义以下宏:
 *
 * - CHM_PREFIX:      函数名的前缀 (例如: i64)
 * - CHM_API_TYPE:    公开的 Map 类型 (例如: I64HashMap)
 * - CHM_STRUCT_TYPE: 内部的 Map 结构体类型 (例如: I64HashMap_t)
 * - CHM_BUCKET_TYPE: 内部的 Bucket 结构体类型 (例如: I64HashMapBucket)
 * - CHM_ENTRY_TYPE:  公开的 Entry 结构体类型 (例如: I64HashMapEntry)
 * - CHM_ITER_TYPE:   公开的 Iter 结构体类型 (例如: I64HashMapIter)
 *
 * * 可选的钩子宏:
 * - CHM_ITER_ASSIGN_ENTRY(entry_out, bucket): 
 * 用于自定义 "从 bucket 赋值到 entry_out" 的逻辑。
 */

// 确保 BucketState 已被包含
#include "utils/hashmap/common.h"

/*
 * ========================================
 * --- 迭代器 API 实现 ---
 * ========================================
 */

/**
 * @brief (内部) 扫描迭代器到下一个 BUCKET_FILLED 的槽位。
 */
static inline void
CHM_FUNC(CHM_PREFIX, iter_scan_to_next)(CHM_ITER_TYPE *iter, const CHM_STRUCT_TYPE *map_internal)
{
  // 模仿 LLVM 的 AdvancePastEmptyBuckets
  while (iter->index < map_internal->num_buckets && map_internal->states[iter->index] != BUCKET_FILLED)
  {
    iter->index++;
  }
}

/**
 * @brief (公共) 初始化一个哈希表迭代器。
 */
CHM_ITER_TYPE // (例如 I64HashMapIter)
CHM_FUNC(CHM_PREFIX, iter)(const CHM_API_TYPE *map)
{
  // 迭代器结构体是 {const API_TYPE *map, size_t index}
  CHM_ITER_TYPE iter = {map, 0};

  // 转换指针以访问内部成员
  const CHM_STRUCT_TYPE *map_internal = (const CHM_STRUCT_TYPE *)map;

  // 立即扫描到第一个有效条目
  CHM_FUNC(CHM_PREFIX, iter_scan_to_next)(&iter, map_internal);

  return iter;
}

/**
 * @brief (公共) 推进迭代器并获取下一个条目。
 */
bool
CHM_FUNC(CHM_PREFIX, iter_next)(CHM_ITER_TYPE *iter, CHM_ENTRY_TYPE *entry_out)
{
  const CHM_STRUCT_TYPE *map_internal = (const CHM_STRUCT_TYPE *)iter->map;

  // 1. 检查是否已在末尾
  if (iter->index >= map_internal->num_buckets)
  {
    return false;
  }

  // 2. 获取当前条目 (我们知道 iter->index 指向一个 FILLED 槽)
  const CHM_BUCKET_TYPE *bucket = &map_internal->buckets[iter->index];

// 使用钩子宏进行赋值
#ifndef CHM_ITER_ASSIGN_ENTRY
  // 默认情况 (int, float, ptr, generic): key 是一个简单字段
  entry_out->key = bucket->key;
  entry_out->value = bucket->value;
#else
  // 特殊情况 (str_slice): key 是复合的
  CHM_ITER_ASSIGN_ENTRY(entry_out, bucket);
#endif

  // 3. 推进并扫描到下一个
  iter->index++;
  CHM_FUNC(CHM_PREFIX, iter_scan_to_next)(iter, map_internal);

  return true;
}

// --- 清理宏 ---
#undef CHM_PREFIX
#undef CHM_API_TYPE
#undef CHM_STRUCT_TYPE
#undef CHM_BUCKET_TYPE
#undef CHM_ENTRY_TYPE
#undef CHM_ITER_TYPE
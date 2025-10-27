/* include/utils/hashmap/common.h */
#ifndef HASHMAP_COMMON_H
#define HASHMAP_COMMON_H

#include <stdint.h>

/**
 * @brief 定义哈希表桶 (bucket) 的状态。
 * 使用一个并行的 'states' 数组 (uint8_t) 来存储这些值,
 * 而不是依赖 Key 本身的“哨兵值”。
 */
typedef enum BucketState
{
  /** @brief 槽位是空的, 从未被使用过。*/
  BUCKET_EMPTY = 0,
  /** @brief 槽位是满的, 包含一个有效的键值对。*/
  BUCKET_FILLED = 1,
  /** @brief 槽位曾被使用, 但现已被删除 (墓碑)。*/
  BUCKET_TOMBSTONE = 2
} BucketState;

#endif // HASHMAP_COMMON_H
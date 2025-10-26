/* include/hashmap/int.h */
#ifndef HASHMAP_INT_H
#define HASHMAP_INT_H

#include "utils/bump.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * =================================================================
 * --- X-Macro 列表：定义所有支持的整数哈希表 ---
 * * 格式: X(PREFIX, K_TYPE, API_TYPE)
 * - PREFIX: 函数前缀 (例如: i64, u64)
 * - K_TYPE: 键的 C 类型 (例如: int64_t, uint64_t)
 * - API_TYPE: 公开的哈希表类型名 (例如: I64HashMap)
 * =================================================================
 */
#define CHASHMAP_INT_TYPES(X)                                                                                          \
  X(i64, int64_t, I64HashMap)                                                                                          \
  X(u64, uint64_t, U64HashMap)                                                                                         \
  X(i32, int32_t, I32HashMap)                                                                                          \
  X(u32, uint32_t, U32HashMap)                                                                                         \
  X(i16, int16_t, I16HashMap)                                                                                          \
  X(u16, uint16_t, U16HashMap)                                                                                         \
  X(i8, int8_t, I8HashMap)                                                                                             \
  X(u8, uint8_t, U8HashMap)                                                                                            \
  X(sz, size_t, SizeHashMap)                                                                                           \
  X(iptr, intptr_t, IPtrHashMap)                                                                                       \
  X(uptr, uintptr_t, UPtrHashMap)

/*
 * --- 1. 声明不透明的结构体类型 ---
 */
#define CHM_DECLARE_INT_TYPEDEF(PREFIX, K_TYPE, API_TYPE) typedef struct API_TYPE API_TYPE;
CHASHMAP_INT_TYPES(CHM_DECLARE_INT_TYPEDEF)
#undef CHM_DECLARE_INT_TYPEDEF

/*
 * --- 2. 声明所有 API 函数 ---
 */
#define CHM_DECLARE_INT_FUNCS(PREFIX, K_TYPE, API_TYPE)                                                                \
                                                                                                                       \
  /**                                                                                                                  \
   * @brief 创建一个新的 [API_TYPE] 哈希表。                                                                 \
   * (所有整数值现在都可以作为 Key 存储。)                                                              \
   */                                                                                                                  \
  API_TYPE *PREFIX##_hashmap_create(Bump *arena, size_t initial_capacity);                                             \
                                                                                                                       \
  /**                                                                                                                  \
   * @brief 插入或更新一个键值对。                                                                          \
   */                                                                                                                  \
  bool PREFIX##_hashmap_put(API_TYPE *map, K_TYPE key, void *value);                                                   \
                                                                                                                       \
  /**                                                                                                                  \
   * @brief 查找一个 Key 对应的 Value。                                                                        \
   */                                                                                                                  \
  void *PREFIX##_hashmap_get(const API_TYPE *map, K_TYPE key);                                                         \
                                                                                                                       \
  /**                                                                                                                  \
   * @brief 从哈希表中移除一个 Key。                                                                         \
   */                                                                                                                  \
  bool PREFIX##_hashmap_remove(API_TYPE *map, K_TYPE key);                                                             \
                                                                                                                       \
  /**                                                                                                                  \
   * @brief 检查一个 Key 是否存在。                                                                           \
   */                                                                                                                  \
  bool PREFIX##_hashmap_contains(const API_TYPE *map, K_TYPE key);                                                     \
                                                                                                                       \
  /**                                                                                                                  \
   * @brief 获取哈希表中的条目数。                                                                          \
   */                                                                                                                  \
  size_t PREFIX##_hashmap_size(const API_TYPE *map);

CHASHMAP_INT_TYPES(CHM_DECLARE_INT_FUNCS)
#undef CHM_DECLARE_INT_FUNCS

// 清理主 X-Macro
#undef CHASHMAP_INT_TYPES

#endif // HASHMAP_INT_H
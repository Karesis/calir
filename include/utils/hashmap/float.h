/* include/hashmap/float.h */
#ifndef HASHMAP_FLOAT_H
#define HASHMAP_FLOAT_H

#include "utils/bump.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * =================================================================
 * --- X-Macro 列表：定义所有支持的浮点哈希表 ---
 * * 格式: X(PREFIX, K_TYPE, API_TYPE)
 * - PREFIX: 函数前缀 (例如: f32, f64)
 * - K_TYPE: 键的 C 类型 (例如: float, double)
 * - API_TYPE: 公开的哈希表类型名 (例如: F32HashMap)
 * =================================================================
 */
#define CHASHMAP_FLOAT_TYPES(X)                                                                                        \
  X(f32, float, F32HashMap)                                                                                            \
  X(f64, double, F64HashMap)

/*
 * --- 1. 声明不透明的结构体类型 ---
 */
#define CHM_DECLARE_FLOAT_TYPEDEF(PREFIX, K_TYPE, API_TYPE) typedef struct API_TYPE API_TYPE;
CHASHMAP_FLOAT_TYPES(CHM_DECLARE_FLOAT_TYPEDEF)
#undef CHM_DECLARE_FLOAT_TYPEDEF

/*
 * --- 2. 声明所有 API 函数 ---
 */
#define CHM_DECLARE_FLOAT_FUNCS(PREFIX, K_TYPE, API_TYPE)                                                              \
                                                                                                                       \
  /**                                                                                                                  \
   * @brief 创建一个新的 [API_TYPE] 哈希表。                                                                 \
   * @warning Key 'NaN' 和 'Inf' 是保留的哨兵值, 不能被存储。                                            \
   */                                                                                                                  \
  API_TYPE *PREFIX##_hashmap_create(Bump *arena, size_t initial_capacity);                                             \
                                                                                                                       \
  /**                                                                                                                  \
   * @brief 插入或更新一个键值对。                                                                          \
   * @warning Key 不能是 NaN 或 Inf。                                                                             \
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

CHASHMAP_FLOAT_TYPES(CHM_DECLARE_FLOAT_FUNCS)
#undef CHM_DECLARE_FLOAT_FUNCS

// 清理主 X-Macro
#undef CHASHMAP_FLOAT_TYPES

#endif // HASHMAP_FLOAT_H
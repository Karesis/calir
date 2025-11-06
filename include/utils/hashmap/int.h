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

/* include/hashmap/int.h */
#pragma once

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
 * --- 声明不透明的结构体类型 ---
 */
#define CHM_DECLARE_INT_TYPEDEF(PREFIX, K_TYPE, API_TYPE) typedef struct API_TYPE API_TYPE;
CHASHMAP_INT_TYPES(CHM_DECLARE_INT_TYPEDEF)
#undef CHM_DECLARE_INT_TYPEDEF

/*
 * --- 声明迭代器结构体 ---
 */
#define CHM_DECLARE_INT_ITER_STRUCTS(PREFIX, K_TYPE, API_TYPE)                                                         \
                                                                                                                       \
  /** @brief [API_TYPE] 的一个条目 (Key/Value 对) */                                                                   \
  typedef struct                                                                                                       \
  {                                                                                                                    \
    K_TYPE key;                                                                                                        \
    void *value;                                                                                                       \
  } API_TYPE##Entry;                                                                                                   \
                                                                                                                       \
  /** @brief [API_TYPE] 的迭代器状态 */                                                                                \
  typedef struct                                                                                                       \
  {                                                                                                                    \
    const API_TYPE *map;                                                                                               \
    size_t index; /* 内部桶数组的当前索引 */                                                                           \
  } API_TYPE##Iter;

CHASHMAP_INT_TYPES(CHM_DECLARE_INT_ITER_STRUCTS)
#undef CHM_DECLARE_INT_ITER_STRUCTS

/*
 * --- 声明所有 API 函数 ---
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
  size_t PREFIX##_hashmap_size(const API_TYPE *map);                                                                   \
                                                                                                                       \
  /**                                                                                                                  \
   * @brief 初始化一个哈希表迭代器。                                                                       \
   */                                                                                                                  \
  API_TYPE##Iter PREFIX##_hashmap_iter(const API_TYPE *map);                                                           \
                                                                                                                       \
  /**                                                                                                                  \
   * @brief 推进迭代器并获取下一个条目。                                                                 \
   */                                                                                                                  \
  bool PREFIX##_hashmap_iter_next(API_TYPE##Iter *iter, API_TYPE##Entry *entry_out);

CHASHMAP_INT_TYPES(CHM_DECLARE_INT_FUNCS)
#undef CHM_DECLARE_INT_FUNCS

#undef CHASHMAP_INT_TYPES

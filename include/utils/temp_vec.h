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


#ifndef CALIR_UTILS_TEMP_VEC_H
#define CALIR_UTILS_TEMP_VEC_H

#include "utils/bump.h"
#include <stdbool.h>
#include <stddef.h>

/**
 * @brief 一个简单的、类型不安全的、可增长的向量 (类似 std::vector<void*>)
 *
 * 它从一个外部传入的 Bump 竞技场上分配内存。
 * 它本身不存储任何数据，只管理指针。
 */
typedef struct TempVec
{
  Bump *arena;
  void **data;
  size_t len;
  size_t capacity;
} TempVec;

/**
 * @brief 初始化一个 TempVec，使其与给定的竞技场关联。
 *
 * @param vec 要初始化的 TempVec 实例
 * @param arena 用于所有分配的 Bump 竞技场
 */
void temp_vec_init(TempVec *vec, Bump *arena);

/**
 * @brief 销毁 TempVec (无操作)。
 *
 * 这是一个无操作函数，因为所有内存都由 Bump 竞技场管理。
 * 它的存在是为了与 string_buf 和其他 utils 保持 API 对称。
 *
 * @param vec 要 "销毁" 的 TempVec
 */
void temp_vec_destroy(TempVec *vec);

/**
 * @brief 向向量末尾添加一个元素。
 *
 * 如果容量不足，将使用 BUMP_REALLOC_SLICE 自动增长。
 *
 * @param vec TempVec 实例
 * @param element 要添加的 (void*) 元素
 * @return true 如果成功, false 如果 OOM (内存溢出)
 */
bool temp_vec_push(TempVec *vec, void *element);

/**
 * @brief (内联) 获取向量中的元素数量。
 */
static inline size_t
temp_vec_len(const TempVec *vec)
{
  return vec->len;
}

/**
 * @brief (内联) 获取指向向量原始数据的指针。
 */
static inline void **
temp_vec_data(const TempVec *vec)
{
  return vec->data;
}

/**
 * @brief (内联) 获取指定索引处的元素。
 *
 * [!!] 注意: 不进行边界检查。
 */
static inline void *
temp_vec_get(const TempVec *vec, size_t index)
{

  return vec->data[index];
}

/**
 * @brief (内联) 清空向量 (将长度重置为 0)。
 *
 * 这不会释放内存，只是允许向量被重用。
 * 这对于解析器在 `bump_reset` 之前重用 TempVec 很有用。
 */
static inline void
temp_vec_clear(TempVec *vec)
{
  vec->len = 0;
}

#endif
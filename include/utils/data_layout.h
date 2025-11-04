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

#ifndef CALIR_DATALAYOUT_H
#define CALIR_DATALAYOUT_H

#include "ir/type.h"
#include "utils/bump.h"
#include <stdbool.h>
#include <stddef.h>

/**
 * @file datalayout.h
 * @brief 计算 IR 类型的内存布局（大小和对齐）。
 *
 * 这是一个可配置的 DataLayout 模块，用于指定目标平台的布局规则。
 * 它不再硬编码依赖于 *宿主* 架构。
 */

/**
 * @brief 存储特定IR类型的布局信息（大小和ABI对齐）
 */
typedef struct
{
  size_t size_in_bytes;
  size_t abi_align_in_bytes;
} TypeLayoutInfo;

/**
 * @brief 定义目标平台的完整数据布局。
 *
 * 编译器/解释器的所有部分都应查询此结构以获取布局信息。
 */
typedef struct
{
  bool is_little_endian;

  TypeLayoutInfo i1_layout;
  TypeLayoutInfo i8_layout;
  TypeLayoutInfo i16_layout;
  TypeLayoutInfo i32_layout;
  TypeLayoutInfo i64_layout;
  TypeLayoutInfo f32_layout;
  TypeLayoutInfo f64_layout;
  TypeLayoutInfo ptr_layout;

  /**
   * @brief 结构体的"最大"或"首选"对齐。
   * 设为 0 表示使用 C 规则（成员的最大对齐）。
   */
  size_t aggregate_preferred_align_in_bytes;

} DataLayout;

/*
 * --- 生命周期 ---
 */

/**
 * @brief 创建一个 DataLayout 实例，其规则匹配 *宿主* 平台。
 *
 * 这提供了与旧的 `get_type_layout` 相同的行为，但现在是显式的。
 * @return DataLayout* 分配在堆上的 DataLayout 实例。
 * @note 调用者必须使用 datalayout_destroy() 释放。
 */
DataLayout *datalayout_create_host(void);

/**
 * @brief 释放由 datalayout_create_...() 创建的 DataLayout 实例。
 * @param dl 要释放的 DataLayout 实例。
 */
void datalayout_destroy(DataLayout *dl);

/*
 * --- 核心 API ---
 */

/**
 * @brief 获取 IRType 在指定 DataLayout 下的大小和对齐
 *
 * @param dl 目标数据布局。
 * @param type 要计算布局的 IRType。
 * @return BumpLayout 包含 .size 和 .align 的结构。
 */
BumpLayout datalayout_get_type_layout(const DataLayout *dl, IRType *type);

/**
 * @brief (便捷函数) 获取 IRType 在指定 DataLayout 下的大小。
 * @param dl 目标数据布局。
 * @param type 要计算的 IRType。
 * @return size_t 类型的大小（字节）。
 */
size_t datalayout_get_type_size(const DataLayout *dl, IRType *type);

/**
 * @brief (便捷函数) 获取 IRType 在指定 DataLayout 下的对齐。
 * @param dl 目标数据布局。
 * @param type 要计算的 IRType。
 * @return size_t 类型的对齐（字节）。
 */
size_t datalayout_get_type_align(const DataLayout *dl, IRType *type);

/**
 * @brief 获取结构体成员的偏移量。
 *
 * @param dl 目标数据布局。
 * @param struct_type 必须是 IR_TYPE_STRUCT 类型。
 * @param member_index 成员的索引。
 * @return size_t 成员相对于结构体起始地址的偏移量（字节）。
 */
size_t datalayout_get_struct_member_offset(const DataLayout *dl, IRType *struct_type, size_t member_index);

/**
 * @brief (便捷函数) 获取指针的大小。
 */
size_t datalayout_get_pointer_size(const DataLayout *dl);

/**
 * @brief (便捷函数) 获取指针的ABI对齐。
 */
size_t datalayout_get_pointer_align(const DataLayout *dl);

#endif

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

#include "utils/data_layout.h"
#include "ir/type.h" // 包含 IRType 的完整定义
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h> // for malloc/free

/// 辅助宏，用于将 value 向上对齐到 align 的倍数
/// align 必须是 2 的幂
#define ALIGN_UP(value, align) (((value) + (align) - 1) & ~((align) - 1))

// 内部辅助函数，用于获取原始类型的布局
static TypeLayoutInfo
get_primitive_layout(const DataLayout *dl, IRType *type)
{
  switch (type->kind)
  {
  case IR_TYPE_I1:
    return dl->i1_layout;
  case IR_TYPE_I8:
    return dl->i8_layout;
  case IR_TYPE_I16:
    return dl->i16_layout;
  case IR_TYPE_I32:
    return dl->i32_layout;
  case IR_TYPE_I64:
    return dl->i64_layout;
  case IR_TYPE_F32:
    return dl->f32_layout;
  case IR_TYPE_F64:
    return dl->f64_layout;
  case IR_TYPE_PTR:
    return dl->ptr_layout;
  default:
    assert(false && "Not a primitive type");
    return (TypeLayoutInfo){0, 1};
  }
}

static bool
is_primitive_type(IRTypeKind kind)
{
  switch (kind)
  {
  case IR_TYPE_I1:
  case IR_TYPE_I8:
  case IR_TYPE_I16:
  case IR_TYPE_I32:
  case IR_TYPE_I64:
  case IR_TYPE_F32:
  case IR_TYPE_F64:
  case IR_TYPE_PTR:
    return true;
  default:
    return false;
  }
}

/*
 * --- 生命周期 ---
 */

DataLayout *
datalayout_create_host(void)
{
  DataLayout *dl = (DataLayout *)malloc(sizeof(DataLayout));
  if (!dl)
    return NULL;

  // 1. 确定 Endianness
  uint32_t i = 1;
  char *c = (char *)&i;
  dl->is_little_endian = (*c == 1);

  // 2. 填充基本类型 (这是唯一允许使用 sizeof/_Alignof 的地方)
  dl->i1_layout = (TypeLayoutInfo){.size_in_bytes = sizeof(bool), .abi_align_in_bytes = _Alignof(bool)};
  dl->i8_layout = (TypeLayoutInfo){.size_in_bytes = sizeof(int8_t), .abi_align_in_bytes = _Alignof(int8_t)};
  dl->i16_layout = (TypeLayoutInfo){.size_in_bytes = sizeof(int16_t), .abi_align_in_bytes = _Alignof(int16_t)};
  dl->i32_layout = (TypeLayoutInfo){.size_in_bytes = sizeof(int32_t), .abi_align_in_bytes = _Alignof(int32_t)};
  dl->i64_layout = (TypeLayoutInfo){.size_in_bytes = sizeof(int64_t), .abi_align_in_bytes = _Alignof(int64_t)};
  dl->f32_layout = (TypeLayoutInfo){.size_in_bytes = sizeof(float), .abi_align_in_bytes = _Alignof(float)};
  dl->f64_layout = (TypeLayoutInfo){.size_in_bytes = sizeof(double), .abi_align_in_bytes = _Alignof(double)};
  dl->ptr_layout = (TypeLayoutInfo){.size_in_bytes = sizeof(void *), .abi_align_in_bytes = _Alignof(void *)};

  // 3. 聚合对齐规则 (使用C规则)
  dl->aggregate_preferred_align_in_bytes = 0;

  return dl;
}

void
datalayout_destroy(DataLayout *dl)
{
  free(dl);
}

/*
 * --- 核心 API ---
 */

BumpLayout
datalayout_get_type_layout(const DataLayout *dl, IRType *type)
{
  // 基本类型：直接查询 DataLayout 结构
  if (is_primitive_type(type->kind))
  {
    TypeLayoutInfo info = get_primitive_layout(dl, type);
    return (BumpLayout){.size = info.size_in_bytes, .align = info.abi_align_in_bytes};
  }

  // 复杂类型：递归计算
  switch (type->kind)
  {
  case IR_TYPE_ARRAY: {
    // 数组的对齐等于其元素的对齐
    BumpLayout elem_layout = datalayout_get_type_layout(dl, type->as.array.element_type);

    return (BumpLayout){.size = elem_layout.size * type->as.array.element_count, .align = elem_layout.align};
  }

  case IR_TYPE_STRUCT: {
    size_t total_size = 0;
    size_t max_align = 1;

    for (size_t i = 0; i < type->as.aggregate.member_count; i++)
    {
      BumpLayout member_layout = datalayout_get_type_layout(dl, type->as.aggregate.member_types[i]);

      // 1. 将当前偏移量 (total_size) 向上对齐到成员的对齐要求
      total_size = ALIGN_UP(total_size, member_layout.align);

      // 2. 累加成员的大小
      total_size += member_layout.size;

      // 3. 更新结构体的最大对齐
      if (member_layout.align > max_align)
      {
        max_align = member_layout.align;
      }
    }

    // 4. 应用聚合对齐规则
    if (dl->aggregate_preferred_align_in_bytes > 0)
    {
      if (dl->aggregate_preferred_align_in_bytes > max_align)
      {
        max_align = dl->aggregate_preferred_align_in_bytes;
      }
    }

    // 5. 将结构体的总大小向上对齐到其最终的对齐
    total_size = ALIGN_UP(total_size, max_align);

    return (BumpLayout){.size = total_size, .align = max_align};
  }

  default:
    // IR_TYPE_VOID, IR_TYPE_FUNCTION, etc.
    assert(false && "Cannot get layout for complex or void type");
    return (BumpLayout){0, 1};
  }
}

size_t
datalayout_get_type_size(const DataLayout *dl, IRType *type)
{
  return datalayout_get_type_layout(dl, type).size;
}

size_t
datalayout_get_type_align(const DataLayout *dl, IRType *type)
{
  return datalayout_get_type_layout(dl, type).align;
}

/**
 * @brief [!!] (已改进) 获取结构体成员的偏移量。
 * (此逻辑现在与您在 interpreter.c 中已验证的旧逻辑相匹配)
 */
size_t
datalayout_get_struct_member_offset(const DataLayout *dl, IRType *struct_type, size_t member_index)
{
  assert(struct_type->kind == IR_TYPE_STRUCT && "Type is not a struct");
  assert(member_index < struct_type->as.aggregate.member_count && "Member index out of bounds");

  size_t current_offset = 0;

  // 1. 累加 0 到 (member_index - 1) 的所有成员
  for (size_t i = 0; i < member_index; i++)
  {
    BumpLayout member_layout = datalayout_get_type_layout(dl, struct_type->as.aggregate.member_types[i]);
    // 1a. 对齐当前偏移量
    current_offset = ALIGN_UP(current_offset, member_layout.align);
    // 1b. 加上大小
    current_offset += member_layout.size;
  }

  // 2. 计算并返回第 *member_index* 个成员的最终对齐偏移量
  BumpLayout final_member_layout = datalayout_get_type_layout(dl, struct_type->as.aggregate.member_types[member_index]);
  current_offset = ALIGN_UP(current_offset, final_member_layout.align);

  return current_offset;
}

size_t
datalayout_get_pointer_size(const DataLayout *dl)
{
  return dl->ptr_layout.size_in_bytes;
}

size_t
datalayout_get_pointer_align(const DataLayout *dl)
{
  return dl->ptr_layout.abi_align_in_bytes;
}

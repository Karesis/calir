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


#include "ir/type.h"
#include "ir/context.h" // 需要 IRContext 结构体
#include "ir/printer.h"
#include "utils/bump.h" // 需要 BUMP_ALLOC_ZEROED
#include <assert.h>
#include <string.h> // for memcpy

/**
 * @brief [内部] 创建一个新的基本类型 (i32, void, ...)
 */
IRType *
ir_type_create_primitive(IRContext *ctx, IRTypeKind kind)
{
  // 基本类型不能是指针类型
  assert(kind != IR_TYPE_PTR && "Use ir_type_create_ptr for pointer types");

  // 从永久 Arena 分配并零初始化
  IRType *type = BUMP_ALLOC_ZEROED(&ctx->permanent_arena, IRType);
  if (!type)
  {
    // OOM error
    return NULL;
  }

  type->kind = kind;
  type->as.pointee_type = NULL; // 零初始化已完成，这里是显式说明
  return type;
}

/**
 * @brief [内部] 创建一个新的指针类型
 */
IRType *
ir_type_create_ptr(IRContext *ctx, IRType *pointee_type)
{
  assert(pointee_type != NULL && "Pointer must point to a type");

  // 从永久 Arena 分配并零初始化
  IRType *type = BUMP_ALLOC_ZEROED(&ctx->permanent_arena, IRType);
  if (!type)
  {
    // OOM error
    return NULL;
  }

  type->kind = IR_TYPE_PTR;
  type->as.pointee_type = pointee_type;
  return type;
}

/**
 * @brief [内部] 创建一个新的数组类型
 */
IRType *
ir_type_create_array(IRContext *ctx, IRType *element_type, size_t element_count)
{
  assert(ctx != NULL);
  assert(element_type != NULL);

  // 从永久 Arena 分配
  IRType *type = BUMP_ALLOC_ZEROED(&ctx->permanent_arena, IRType);
  if (!type)
    return NULL; // OOM

  type->kind = IR_TYPE_ARRAY;
  type->as.array.element_type = element_type;
  type->as.array.element_count = element_count;

  return type;
}

/**
 * @brief [内部] 创建一个新的结构体类型
 */
IRType *
ir_type_create_struct(IRContext *ctx, IRType **member_types, size_t member_count, const char *name)
{
  assert(ctx != NULL);
  assert(member_types != NULL || member_count == 0);

  // 1. 分配 Type 结构体本身
  IRType *type = BUMP_ALLOC_ZEROED(&ctx->permanent_arena, IRType);
  if (!type)
    return NULL; // OOM

  type->kind = IR_TYPE_STRUCT;

  // 2. 分配并拷贝成员类型数组
  if (member_count > 0)
  {
    // 在 permanent_arena 中创建这个数组的*副本*
    type->as.aggregate.member_types = BUMP_ALLOC_SLICE(&ctx->permanent_arena, IRType *, member_count);
    if (!type->as.aggregate.member_types)
      return NULL; // OOM

    memcpy(type->as.aggregate.member_types, member_types, member_count * sizeof(IRType *));
  }
  else
  {
    type->as.aggregate.member_types = NULL;
  }
  type->as.aggregate.member_count = member_count;

  // 3. (可选) Intern 结构体名字
  if (name)
  {
    type->as.aggregate.name = ir_context_intern_str(ctx, name);
  }
  else
  {
    type->as.aggregate.name = NULL;
  }

  return type;
}

/**
 * @brief [!!] [内部] 创建一个新的函数类型
 */
IRType *
ir_type_create_function(IRContext *ctx, IRType *return_type, IRType **param_types, size_t param_count, bool is_variadic)
{
  assert(ctx != NULL);
  assert(return_type != NULL);
  assert(param_types != NULL || param_count == 0);

  // 1. 分配 Type 结构体本身
  IRType *type = BUMP_ALLOC_ZEROED(&ctx->permanent_arena, IRType);
  if (!type)
    return NULL; // OOM

  type->kind = IR_TYPE_FUNCTION;

  // 2. 设置函数特定成员
  type->as.function.return_type = return_type;
  type->as.function.is_variadic = is_variadic;

  // 3. 分配并拷贝参数类型数组 (逻辑同 struct)
  if (param_count > 0)
  {
    // 在 permanent_arena 中创建这个数组的*副本*
    type->as.function.param_types = BUMP_ALLOC_SLICE(&ctx->permanent_arena, IRType *, param_count);
    if (!type->as.function.param_types)
      return NULL; // OOM

    memcpy(type->as.function.param_types, param_types, param_count * sizeof(IRType *));
  }
  else
  {
    type->as.function.param_types = NULL;
  }
  type->as.function.param_count = param_count;

  return type;
}

/*
 * =================================================================
 * --- 调试 API ---
 * =================================================================
 */

/**
 * @brief [!!] 重构 [!!]
 * 将类型直接打印到 IRPrinter (机制)。
 * (此函数现在包含了 'ir_type_to_string_recursive' 的所有逻辑)
 *
 * @param type 要打印的类型
 * @param p 打印机 (策略)
 */
void
ir_type_dump(IRType *type, IRPrinter *p)
{
  if (!type)
  {
    ir_print_str(p, "<null type>");
    return;
  }

  switch (type->kind)
  {
  // --- 基本类型 ---
  case IR_TYPE_VOID:
    ir_print_str(p, "void");
    break;
  case IR_TYPE_I1:
    ir_print_str(p, "i1");
    break;
  case IR_TYPE_I8:
    ir_print_str(p, "i8");
    break;
  case IR_TYPE_I16:
    ir_print_str(p, "i16");
    break;
  case IR_TYPE_I32:
    ir_print_str(p, "i32");
    break;
  case IR_TYPE_I64:
    ir_print_str(p, "i64");
    break;
  case IR_TYPE_F32:
    ir_print_str(p, "f32");
    break;
  case IR_TYPE_F64:
    ir_print_str(p, "f64");
    break;
  case IR_TYPE_LABEL:
    ir_print_str(p, "label");
    break;

  // --- 派生类型 ---
  case IR_TYPE_PTR:
    // <...>
    ir_print_str(p, "<");
    ir_type_dump(type->as.pointee_type, p); // 递归调用
    ir_print_str(p, ">");
    break;

  case IR_TYPE_ARRAY:
    // [10 x i32]
    ir_print_str(p, "[");
    ir_printf(p, "%zu", type->as.array.element_count); // [!!] 直接打印
    ir_print_str(p, " x ");
    ir_type_dump(type->as.array.element_type, p); // 递归调用
    ir_print_str(p, "]");
    break;

  case IR_TYPE_STRUCT:
    // 命名结构体: %my_struct
    // (注意: 'module.c' 负责打印 *定义*，这里只打印 *用法*)
    if (type->as.aggregate.name)
    {
      ir_print_str(p, "%");
      ir_print_str(p, type->as.aggregate.name);
      break;
    }

    // 匿名结构体: { i32, <f64> }
    ir_print_str(p, "{ ");
    for (size_t i = 0; i < type->as.aggregate.member_count; i++)
    {
      if (i > 0)
      {
        ir_print_str(p, ", ");
      }
      ir_type_dump(type->as.aggregate.member_types[i], p); // 递归调用
    }
    ir_print_str(p, " }");
    break;

  case IR_TYPE_FUNCTION:
    // i32 (i32, f64, ...)
    ir_type_dump(type->as.function.return_type, p); // 递归调用
    ir_print_str(p, " (");
    for (size_t i = 0; i < type->as.function.param_count; i++)
    {
      if (i > 0)
      {
        ir_print_str(p, ", ");
      }
      ir_type_dump(type->as.function.param_types[i], p); // 递归调用
    }
    if (type->as.function.is_variadic)
    {
      if (type->as.function.param_count > 0)
      {
        ir_print_str(p, ", ");
      }
      ir_print_str(p, "...");
    }
    ir_print_str(p, ")");
    break;

  default:
    ir_print_str(p, "?");
    break;
  }
}
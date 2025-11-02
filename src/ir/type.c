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
#include "ir/context.h"
#include "ir/printer.h"
#include "utils/bump.h"
#include <assert.h>
#include <string.h>

/**
 * @brief [内部] 创建一个新的基本类型 (i32, void, ...)
 */
IRType *
ir_type_create_primitive(IRContext *ctx, IRTypeKind kind)
{

  assert(kind != IR_TYPE_PTR && "Use ir_type_create_ptr for pointer types");


  IRType *type = BUMP_ALLOC_ZEROED(&ctx->permanent_arena, IRType);
  if (!type)
  {

    return NULL;
  }

  type->kind = kind;
  type->as.pointee_type = NULL;
  return type;
}

/**
 * @brief [内部] 创建一个新的指针类型
 */
IRType *
ir_type_create_ptr(IRContext *ctx, IRType *pointee_type)
{
  assert(pointee_type != NULL && "Pointer must point to a type");


  IRType *type = BUMP_ALLOC_ZEROED(&ctx->permanent_arena, IRType);
  if (!type)
  {

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


  IRType *type = BUMP_ALLOC_ZEROED(&ctx->permanent_arena, IRType);
  if (!type)
    return NULL;

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


  IRType *type = BUMP_ALLOC_ZEROED(&ctx->permanent_arena, IRType);
  if (!type)
    return NULL;

  type->kind = IR_TYPE_STRUCT;


  if (member_count > 0)
  {

    type->as.aggregate.member_types = BUMP_ALLOC_SLICE(&ctx->permanent_arena, IRType *, member_count);
    if (!type->as.aggregate.member_types)
      return NULL;

    memcpy(type->as.aggregate.member_types, member_types, member_count * sizeof(IRType *));
  }
  else
  {
    type->as.aggregate.member_types = NULL;
  }
  type->as.aggregate.member_count = member_count;


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


  IRType *type = BUMP_ALLOC_ZEROED(&ctx->permanent_arena, IRType);
  if (!type)
    return NULL;

  type->kind = IR_TYPE_FUNCTION;


  type->as.function.return_type = return_type;
  type->as.function.is_variadic = is_variadic;


  if (param_count > 0)
  {

    type->as.function.param_types = BUMP_ALLOC_SLICE(&ctx->permanent_arena, IRType *, param_count);
    if (!type->as.function.param_types)
      return NULL;

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


  case IR_TYPE_PTR:

    ir_print_str(p, "<");
    ir_type_dump(type->as.pointee_type, p);
    ir_print_str(p, ">");
    break;

  case IR_TYPE_ARRAY:

    ir_print_str(p, "[");
    ir_printf(p, "%zu", type->as.array.element_count);
    ir_print_str(p, " x ");
    ir_type_dump(type->as.array.element_type, p);
    ir_print_str(p, "]");
    break;

  case IR_TYPE_STRUCT:


    if (type->as.aggregate.name)
    {
      ir_print_str(p, "%");
      ir_print_str(p, type->as.aggregate.name);
      break;
    }


    ir_print_str(p, "{ ");
    for (size_t i = 0; i < type->as.aggregate.member_count; i++)
    {
      if (i > 0)
      {
        ir_print_str(p, ", ");
      }
      ir_type_dump(type->as.aggregate.member_types[i], p);
    }
    ir_print_str(p, " }");
    break;

  case IR_TYPE_FUNCTION:

    ir_type_dump(type->as.function.return_type, p);
    ir_print_str(p, " (");
    for (size_t i = 0; i < type->as.function.param_count; i++)
    {
      if (i > 0)
      {
        ir_print_str(p, ", ");
      }
      ir_type_dump(type->as.function.param_types[i], p);
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
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


#include "ir/constant.h"
#include "ir/context.h"
#include "ir/type.h"
#include "ir/value.h"
#include "utils/bump.h"
#include "utils/id_list.h"
#include <assert.h>
#include <math.h>
#include <string.h>

/**
 * @brief [内部] 创建一个新的 'undef' 常量。
 */
IRValueNode *
ir_constant_create_undef(IRContext *ctx, IRType *type)
{
  assert(type != NULL);


  IRConstant *konst = BUMP_ALLOC_ZEROED(&ctx->permanent_arena, IRConstant);
  if (!konst)
    return NULL;


  konst->value.kind = IR_KIND_CONSTANT;
  konst->value.type = type;
  konst->value.name = NULL;


  list_init(&konst->value.uses);


  konst->const_kind = CONST_KIND_UNDEF;

  return (IRValueNode *)konst;
}

/**
 * @brief [内部] 创建一个新的整数常量 (i1 到 i64)。
 */
IRValueNode *
ir_constant_create_int(IRContext *ctx, IRType *type, int64_t value)
{
  assert(type != NULL);
  assert(type->kind >= IR_TYPE_I1 && type->kind <= IR_TYPE_I64 && "Type must be an integer type");

  IRConstant *konst = BUMP_ALLOC_ZEROED(&ctx->permanent_arena, IRConstant);
  if (!konst)
    return NULL;


  konst->value.kind = IR_KIND_CONSTANT;
  konst->value.type = type;
  konst->value.name = NULL;


  list_init(&konst->value.uses);


  konst->const_kind = CONST_KIND_INT;
  konst->data.int_val = value;

  return (IRValueNode *)konst;
}

/**
 * @brief [内部] 创建一个新的浮点常量 (f32 或 f64)。
 */
IRValueNode *
ir_constant_create_float(IRContext *ctx, IRType *type, double value)
{
  assert(type != NULL);
  assert((type->kind == IR_TYPE_F32 || type->kind == IR_TYPE_F64) && "Type must be a float type");

  IRConstant *konst = BUMP_ALLOC_ZEROED(&ctx->permanent_arena, IRConstant);
  if (!konst)
    return NULL;


  konst->value.kind = IR_KIND_CONSTANT;
  konst->value.type = type;
  konst->value.name = NULL;


  list_init(&konst->value.uses);


  konst->const_kind = CONST_KIND_FLOAT;


  if (type->kind == IR_TYPE_F32)
  {
    konst->data.float_val = (double)((float)value);
  }
  else
  {
    konst->data.float_val = value;
  }

  return (IRValueNode *)konst;
}
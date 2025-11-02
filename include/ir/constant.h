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


#ifndef IR_CONSTANT_H
#define IR_CONSTANT_H

#include "ir/context.h"
#include "ir/type.h"
#include "ir/value.h" // 包含 IRValueNode, IRValueKind
#include <stdbool.h>
#include <stdint.h> // for int64_t, etc.

/**
 * @brief 区分不同类型的常量
 */
typedef enum
{
  /** @brief 'undef' (未定义) 值。没有 'data'。 */
  CONST_KIND_UNDEF,

  /** @brief 整数常量 (i1, i8, ..., i64)。 */
  CONST_KIND_INT,

  /** @brief 浮点常量 (f32, f64)。 */
  CONST_KIND_FLOAT,

} IRConstantKind;

/**
 * @brief 代表一个常量值 (如 5, 3.14, 或 undef)
 * "继承" (内嵌) IRValueNode
 */
typedef struct IRConstant
{
  /** 基类 (value.kind 必须是 IR_KIND_CONSTANT) */
  IRValueNode value;
  /** 此常量的具体种类 */
  IRConstantKind const_kind;

  /** 存储常量的值 */
  union {
    /** 用于 CONST_KIND_INT (i1 到 i64) */
    int64_t int_val;
    /** 用于 CONST_KIND_FLOAT (f32 和 f64) */
    double float_val;
  } data;

} IRConstant;

/*
 * =================================================================
 * --- 内部构造函数 (Internal Constructors) ---
 * =================================================================
 */

/**
 * @brief [内部] 创建一个新的 'undef' 常量。
 * @param ctx Context (用于内存分配)
 * @param type 'undef' 值的类型
 * @return 指向新常量的 IRValueNode*
 */
IRValueNode *ir_constant_create_undef(IRContext *ctx, IRType *type);

/**
 * @brief [内部] 创建一个新的整数常量 (i1 到 i64)。
 * @param ctx Context (用于内存分配)
 * @param type 必须是整数类型 (e.g., ctx->type_i32)
 * @param value 整数的值
 * @return 指向新常量的 IRValueNode*
 */
IRValueNode *ir_constant_create_int(IRContext *ctx, IRType *type, int64_t value);

/**
 * @brief [内部] 创建一个新的浮点常量 (f32 或 f64)。
 * @param ctx Context (用于内存分配)
 * @param type 必须是浮点类型 (e.g., ctx->type_f32)
 * @param value 浮点的值
 * @return 指向新常量的 IRValueNode*
 */
IRValueNode *ir_constant_create_float(IRContext *ctx, IRType *type, double value);

#endif // IR_CONSTANT_H
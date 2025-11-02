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

#ifndef FUNCTION_H
#define FUNCTION_H

#include "ir/module.h"
#include "ir/printer.h"
#include "ir/type.h"
#include "ir/value.h"
#include "utils/id_list.h"

/**
 * @brief 函数
 */
typedef struct IRFunction
{
  IRValueNode entry_address;
  IRModule *parent;

  IRType *return_type;
  IRType *function_type;

  IDList list_node;
  IDList arguments;
  IDList basic_blocks;
} IRFunction;

/**
 * @brief 函数参数
 */
typedef struct IRArgument
{
  IRValueNode value;
  IDList list_node;
  IRFunction *parent;
} IRArgument;

/**
 * @brief [!!] API 恢复 !!
 * 创建一个新函数 (在 Arena 中), *但不* 最终确定其类型。
 *
 * @param mod 父模块
 * @param name 函数名
 * @param ret_type *仅仅* 是返回类型 (e.g., i32)
 * @return 指向新 IRFunction 的指针
 */
IRFunction *ir_function_create(IRModule *mod, const char *name, IRType *ret_type);

/**
 * @brief [!!] API 恢复 !!
 * 向一个 *未定稿* 的函数添加一个参数。
 */
IRArgument *ir_argument_create(IRFunction *func, IRType *type, const char *name);

/**
 * @brief [!!] 新增 API !!
 *
 * 在所有参数都已添加后，“定稿”函数的签名。
 * 此函数会计算完整的 IR_TYPE_FUNCTION，并*最终*设置
 * func->entry_address.type (以修复 'call' bug)。
 *
 * @param func 要定稿的函数
 * @param is_variadic 是否为可变参数
 */
void ir_function_finalize_signature(IRFunction *func, bool is_variadic);

void ir_function_dump(IRFunction *func, IRPrinter *p);

#endif
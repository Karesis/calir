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

#ifndef IR_GLOBAL_H
#define IR_GLOBAL_H

#include "ir/context.h"
#include "ir/module.h"
#include "ir/printer.h"
#include "ir/type.h"
#include "ir/value.h"
#include "utils/id_list.h"

/**
 * @brief 全局变量
 *
 * 代表一个在模块级别的具名变量
 * 它的 IRValueNode (value) 代表它的 *地址*。
 */
typedef struct IRGlobalVariable
{
  IRValueNode value;

  IDList list_node;
  IRModule *parent;

  IRType *allocated_type;
  IRValueNode *initializer;

} IRGlobalVariable;

/**
 * @brief 创建一个新的全局变量 (在 Arena 中)
 *
 * @param mod 父模块 (用于获取 Context 和添加到链表)
 * @param name 全局变量的名字 (e.g., "my_global")
 * @param allocated_type 全局变量要分配的类型 (e.g., i32, [10 x i32], ...)
 * @param initializer (可选) 初始值。必须是一个 Constant。如果为 NULL,
 * 则默认为零初始化 (zeroinitializer)。
 * @return 指向新全局变量的指针 (其 value.type 是 allocated_type 的指针类型)
 */
IRGlobalVariable *ir_global_variable_create(IRModule *mod, const char *name, IRType *allocated_type,
                                            IRValueNode *initializer);

/**
 * @brief 将单个全局变量的 IR 打印到 IRPrinter
 * [!!] 签名已更改
 * @param global 要打印的全局变量
 * @param p 打印机 (策略)
 */
void ir_global_variable_dump(IRGlobalVariable *global, IRPrinter *p);

#endif
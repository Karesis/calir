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


#ifndef VALUE_H
#define VALUE_H

#include "ir/printer.h"
#include "ir/type.h"
#include "utils/id_list.h"

/**
 * @brief 区分 IRValueNode 到底“是”什么
 *
 * 这用于实现安全的向下转型 (down-casting)。
 * (例如，从 IRValueNode* 转换回 IRInstruction*)
 */
typedef enum
{
  IR_KIND_ARGUMENT,
  IR_KIND_INSTRUCTION,
  IR_KIND_BASIC_BLOCK,
  IR_KIND_FUNCTION,
  IR_KIND_CONSTANT,
  IR_KIND_GLOBAL,
} IRValueKind;

/**
 * @brief 所有 IR 对象的基类
 * 代表一个可以被“使用”并具有“类型”的值。
 */
typedef struct
{
  IRValueKind kind;
  const char *name;
  IRType *type;
  IDList uses;

} IRValueNode;

/**
 * @brief 打印一个 Value 的 "名字" (e.g., "%a", "@main", "$entry", "10")
 * [!!] 签名已更改
 * @param p 打印机 (策略)
 */
void ir_value_dump_name(IRValueNode *val, IRPrinter *p);

/**
 * @brief 打印一个 Value 作为 "操作数" (e.g., "%a: i32", "10: i32", "$entry")
 *
 * 负责打印 "name: type" 格式。
 * (内部会智能处理 $label 和 @func, 它们在使用时不需要类型)
 */
void ir_value_dump_with_type(IRValueNode *val, IRPrinter *p);

/**
 * @brief [废弃/可选] 打印一个 Value 的引用 (e.g., "i32 %foo" or "label %entry")
 * (这个函数将被 ir_value_dump_with_type 替代)
 */
void ir_value_dump(IRValueNode *val, IRPrinter *p);

/**
 * @brief 安全地设置 Value 的名字 (自动 free 旧名字并 strdup 新名字)
 * @param val 要修改的 Value
 * @param name 新的名字
 */
void ir_value_set_name(IRValueNode *val, const char *name);

/**
 * @brief (核心优化函数) 替换所有对 old_val 的使用为 new_val
 *
 * 遍历 old_val->uses 链表, 将每个 Use 重新指向 new_val 的 uses 链表。
 * 这是实现常量折叠、指令合并等优化的基础。
 *
 * @param old_val 将被替换的 Value
 * @param new_val 替换后的新 Value
 */
void ir_value_replace_all_uses_with(IRValueNode *old_val, IRValueNode *new_val);

#endif

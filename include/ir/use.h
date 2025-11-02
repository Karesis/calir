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

#ifndef IR_USE_H
#define IR_USE_H

#include "ir/context.h"
#include "ir/instruction.h"
#include "ir/value.h"
#include "utils/id_list.h"

/**
 * @brief "Use" 边 (Def-Use 链的核心)
 *
 * 代表一个 "User" (例如一条指令) 对一个 "Value" (例如 %a) 的使用。
 *
 * 它同时存在于两个链表中：
 * 1. Value 的 "uses" 链表 (通过 value_node)
 * 2. User 的 "operands" 链表 (通过 user_node)
 */
typedef struct IRUse
{
  /** 指向被使用的 Value (Def) */
  IRValueNode *value;
  /** 指向使用方 (User) (目前仅支持指令) */
  IRInstruction *user;

  /** 在 Value->uses 链表中的节点 */
  IDList value_node;
  /** 在 User->operands 链表中的节点 */
  IDList user_node;

} IRUse;

/**
 * @brief [内部] 创建一个 Use 边 (在 Arena 中)
 *
 * 这将自动将 Use 边添加到 Value 和 User 的链表中。
 *
 * @param ctx Context (用于 Arena 分配)
 * @param user 使用此 Value 的指令
 * @param value 被使用的 Value
 * @return 指向新 IRUse 的指针 (或 OOM 时返回 NULL)
 */
IRUse *ir_use_create(IRContext *ctx, IRInstruction *user, IRValueNode *value);

/**
 * @brief [内部] 将 Use 边从两个链表中完全解开
 * @param use 要解开的 Use 边
 */
void ir_use_unlink(IRUse *use);

/**
 * @brief [内部] 更改 Use 边指向的 Value
 *
 * 这是 ir_value_replace_all_uses_with 的核心。
 * 它会从 old_val->uses 解开, 并链接到 new_val->uses。
 *
 * @param use 要修改的 Use 边
 * @param new_val 新的目标 Value
 */
void ir_use_set_value(IRUse *use, IRValueNode *new_val);

#endif
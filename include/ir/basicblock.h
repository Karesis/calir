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

#ifndef BASIC_BLOCK_H
#define BASIC_BLOCK_H

#include "ir/function.h"
#include "ir/printer.h"
#include "ir/value.h"
#include "utils/id_list.h"

/**
 * @brief 基本块
 */
typedef struct IRBasicBlock
{
  IRValueNode label_address;
  IDList list_node;

  IDList instructions;
  IRFunction *parent;
} IRBasicBlock;

/**
 * @brief 创建一个新基本块 (在 Arena 中)
 * @param func 父函数 (用于获取 Context 和添加到链表)
 * @param name 基本块的标签名 (将被 intern)
 * @return 指向新基本块的指针
 */
IRBasicBlock *ir_basic_block_create(IRFunction *func, const char *name);

/**
 * @brief 将一个基本块附加到其父函数的末尾
 * @param func 父函数
 * @param bb 要附加的基本块
 */
void ir_function_append_basic_block(IRFunction *func, IRBasicBlock *bb);

/**
 * @brief 将单个基本块的 IR 打印到 IRPrinter
 * [!!] 签名已更改
 * @param bb 要打印的基本块
 * @param p 打印机 (策略)
 */
void ir_basic_block_dump(IRBasicBlock *bb, IRPrinter *p);

#endif
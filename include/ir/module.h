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


#ifndef MODULE_H
#define MODULE_H

#include "context.h"
#include "ir/printer.h"
#include "utils/id_list.h"
#include <stdio.h>

/**
 * @brief 模块 (翻译单元)
 *
 * 这是 IR 对象的根容器。
 * 它持有指向其 "父" Context 的指针。
 */
typedef struct IRModule
{
  IRContext *context; // <-- [新] 指向拥有它的 Context
  const char *name;   // <-- [修改] 指向 Context 中 interned 的字符串
  IDList functions;   // 链表头 (元素是 IRFunction)
  IDList globals;     // 链表头 (元素是 IRGlobalVariable)
} IRModule;

/**
 * @brief 创建一个新模块 (Module)
 *
 * 模块本身将在 Context 的 'ir_arena' 中分配。
 * 它的名字将被 interned 到 Context 中。
 *
 * @param ctx 模块所属的 IR Context
 * @param name 模块的名称 (将被 intern)
 * @return 指向新模块的指针
 */
IRModule *ir_module_create(IRContext *ctx, const char *name);

/**
 * @brief [策略 1] 将模块的 IR 打印到指定的流 (例如 stdout)
 * (这是旧的 ir_module_dump)
 */
void ir_module_dump_to_file(IRModule *mod, FILE *stream);

/**
 * @brief [策略 2] 将模块的 IR 打印到 arena 上的新字符串
 *
 * @param mod 要打印的模块
 * @param arena 用于分配字符串的 Bump arena
 * @return const char* 指向 arena 上的、以 '\0' 结尾的字符串
 */
const char *ir_module_dump_to_string(IRModule *mod, Bump *arena);

/**
 * @brief [内部机制] 核心 dump 函数。
 * (除非你正在实现一个新的 IRPrinter 策略，否则不应直接调用)
 */
void ir_module_dump_internal(IRModule *mod, IRPrinter *p);

#endif
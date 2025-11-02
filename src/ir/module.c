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


// src/ir/module.c
#include "ir/module.h"
#include "ir/context.h"  // 核心依赖
#include "ir/function.h" // 需要 function_dump
#include "ir/global.h"
#include "ir/printer.h"
#include "ir/type.h"
#include "utils/bump.h" // 需要 BUMP_ALLOC 和 BUMP_ALLOC_ZEROED
#include "utils/hashmap.h"
#include "utils/string_buf.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief 创建一个新模块 (Module)
 */
IRModule *
ir_module_create(IRContext *ctx, const char *name)
{
  assert(ctx != NULL && "IRContext cannot be NULL");

  // 1. 从 ir_arena 分配
  IRModule *mod = BUMP_ALLOC_ZEROED(&ctx->ir_arena, IRModule);
  if (!mod)
  {
    return NULL; // OOM
  }

  // 2. 存储 Context 指针
  mod->context = ctx;

  // 3. 唯一化(Intern)字符串
  mod->name = ir_context_intern_str(ctx, name);
  if (!mod->name)
  {
    // OOM (虽然在 Arena 模型中不太可能发生)
    // 注意：我们无法 "free(mod)"，因为它在 Arena 中
    return NULL;
  }

  // 4. BUMP_ALLOC_ZEROED 已将 prev/next 设为 NULL
  // 我们必须显式调用 list_init 使它们指向自己。
  list_init(&mod->functions);
  list_init(&mod->globals);

  return mod;
}

/**
 * @brief [内部机制] 核心 dump 函数
 */
void
ir_module_dump_internal(IRModule *mod, IRPrinter *p)
{
  if (!mod)
  {
    ir_print_str(p, "; <null module>\n");
    return;
  }

  ir_printf(p, "module = \"%s\"\n", mod->name); // [!!] 已更改
  ir_print_str(p, "\n");                        // [!!] 已更改

  // 遍历并打印所有命名结构体
  StrHashMap *struct_cache = mod->context->named_struct_cache;
  if (struct_cache && str_hashmap_size(struct_cache) > 0)
  {
    StrHashMapIter iter = str_hashmap_iter(struct_cache);
    StrHashMapEntry entry;

    while (str_hashmap_iter_next(&iter, &entry))
    {
      IRType *type = (IRType *)entry.value;

      if (type->kind == IR_TYPE_STRUCT && type->as.aggregate.name)
      {
        ir_printf(p, "%%%s = type ", type->as.aggregate.name); // [!!] 已更改
        ir_print_str(p, "{ ");                                 // [!!] 已更改

        for (size_t i = 0; i < type->as.aggregate.member_count; i++)
        {
          if (i > 0)
          {
            ir_print_str(p, ", "); // [!!] 已更改
          }
          ir_type_dump(type->as.aggregate.member_types[i], p); // [!!] 已更改
        }
        ir_print_str(p, " }\n"); // [!!] 已更改
      }
    }
    ir_print_str(p, "\n"); // [!!] 已更改
  }

  // 打印所有全局变量
  IDList *global_iter;
  list_for_each(&mod->globals, global_iter)
  {
    IRGlobalVariable *global = list_entry(global_iter, IRGlobalVariable, list_node);
    ir_global_variable_dump(global, p); // [!!] 已更改
  }
  if (!list_empty(&mod->globals))
  {
    ir_print_str(p, "\n"); // [!!] 已更改
  }

  // 打印所有函数
  IDList *iter_func;
  list_for_each(&mod->functions, iter_func)
  {
    IRFunction *func = list_entry(iter_func, IRFunction, list_node);
    ir_function_dump(func, p); // [!!] 已更改
  }
}

/*
 * --- [!!] 新的公共 API 实现 [!!] ---
 */

/**
 * @brief [策略 1] 打印到 FILE*
 */
void
ir_module_dump_to_file(IRModule *mod, FILE *stream)
{
  IRPrinter p;
  ir_printer_init_file(&p, stream); // [!!] 设置 FILE 策略
  ir_module_dump_internal(mod, &p); // [!!] 调用核心机制
}

/**
 * @brief [策略 2] 打印到 StringBuf*
 */
const char *
ir_module_dump_to_string(IRModule *mod, Bump *arena)
{
  // 1. 在 arena 上创建 StringBuf (它在栈上，但其 data 在 arena 上)
  StringBuf buf;
  string_buf_init(&buf, arena);

  // 2. 初始化打印机
  IRPrinter p;
  ir_printer_init_string_buf(&p, &buf); // [!!] 设置 StringBuf 策略

  // 3. 调用核心机制
  ir_module_dump_internal(mod, &p);

  // 4. 从 StringBuf 获取最终的、持久的字符串
  return string_buf_get(&buf);
}
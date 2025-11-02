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


#include "ir/global.h"

#include "ir/context.h" // 需要 ir_context_intern_str, ir_type_get_ptr
#include "ir/module.h"
#include "ir/printer.h"
#include "ir/type.h"
#include "ir/value.h"   // 需要 IR_KIND_CONSTANT
#include "utils/bump.h" // 需要 BUMP_ALLOC_ZEROED

#include <assert.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief 创建一个新的全局变量 (在 Arena 中)
 */
IRGlobalVariable *
ir_global_variable_create(IRModule *mod, const char *name, IRType *allocated_type, IRValueNode *initializer)
{
  assert(mod != NULL && "Parent module cannot be NULL");
  IRContext *ctx = mod->context; // 从父级获取 Context

  // 1. [!!] 健全性检查 (Sanity Checks)
  assert(allocated_type != NULL && allocated_type->kind != IR_TYPE_VOID);

  // 初始值 (如果提供) 必须是一个 "全局链接" 的常量
  assert(initializer == NULL || initializer->kind == IR_KIND_CONSTANT || // e.g., 10, undef
         initializer->kind == IR_KIND_FUNCTION ||                        // e.g., @my_func
         initializer->kind == IR_KIND_GLOBAL);                           // e.g., @other_global

  // 初始值 (如果提供) 类型必须匹配
  assert(initializer == NULL || initializer->type == allocated_type);

  // 2. 从 ir_arena 分配
  IRGlobalVariable *global = BUMP_ALLOC_ZEROED(&ctx->ir_arena, IRGlobalVariable);
  if (!global)
    return NULL;

  // 3. 设置子类成员
  global->parent = mod;
  global->allocated_type = allocated_type;
  global->initializer = initializer;

  // 4. [修改] 显式初始化链表
  list_init(&global->list_node);

  // 5. 初始化 IRValueNode 基类
  global->value.kind = IR_KIND_GLOBAL;
  global->value.name = ir_context_intern_str(ctx, name); // Intern 名字
  list_init(&global->value.uses);                        // 显式初始化

  // 6. [!!] 设置 Value 的类型
  // 全局变量的 "Value" 是它的 *地址*，所以它的类型是一个 *指针*
  // (这与 alloca 的行为一致)
  global->value.type = ir_type_get_ptr(ctx, allocated_type);

  // 7. 添加到父模块的全局变量链表
  list_add_tail(&mod->globals, &global->list_node);

  return global;
}

/**
 * @brief [!!] 重构 [!!]
 * 将单个全局变量的 IR 打印到 IRPrinter
 *
 * @param global 要打印的全局变量
 * @param p 打印机 (策略)
 */
void
ir_global_variable_dump(IRGlobalVariable *global, IRPrinter *p)
{
  if (!global)
  {
    ir_print_str(p, "<null global>\n"); // [!!] 已更改
    return;
  }

  // 1. 打印名字 (e.g., "@my_global")
  // [!!] 假设: ir_value_dump_name 已被重构
  ir_value_dump_name(&global->value, p);
  ir_print_str(p, " = "); // [!!] 已更改

  // 2. 打印 "global" 和类型 (e.g., "global i32")
  ir_print_str(p, "global "); // [!!] 已更改

  // [!!] 调用已重构的 ir_type_dump
  ir_type_dump(global->allocated_type, p);

  // 3. 打印初始值
  if (global->initializer)
  {
    ir_print_str(p, " "); // [!!] 已更改

    // [!!] 假设: ir_value_dump_with_type 已被重构
    // (它会打印 "123: i32" 或 "@other_func" 等)
    ir_value_dump_with_type(global->initializer, p);
  }
  else
  {
    ir_print_str(p, " zeroinitializer"); // [!!] 已更改
  }

  ir_print_str(p, "\n"); // [!!] 已更改
}
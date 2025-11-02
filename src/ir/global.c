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

#include "ir/context.h"
#include "ir/module.h"
#include "ir/printer.h"
#include "ir/type.h"
#include "ir/value.h"
#include "utils/bump.h"

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
  IRContext *ctx = mod->context;

  assert(allocated_type != NULL && allocated_type->kind != IR_TYPE_VOID);

  assert(initializer == NULL || initializer->kind == IR_KIND_CONSTANT || initializer->kind == IR_KIND_FUNCTION ||
         initializer->kind == IR_KIND_GLOBAL);

  assert(initializer == NULL || initializer->type == allocated_type);

  IRGlobalVariable *global = BUMP_ALLOC_ZEROED(&ctx->ir_arena, IRGlobalVariable);
  if (!global)
    return NULL;

  global->parent = mod;
  global->allocated_type = allocated_type;
  global->initializer = initializer;

  list_init(&global->list_node);

  global->value.kind = IR_KIND_GLOBAL;
  global->value.name = ir_context_intern_str(ctx, name);
  list_init(&global->value.uses);

  global->value.type = ir_type_get_ptr(ctx, allocated_type);

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
    ir_print_str(p, "<null global>\n");
    return;
  }

  ir_value_dump_name(&global->value, p);
  ir_print_str(p, " = ");

  ir_print_str(p, "global ");

  ir_type_dump(global->allocated_type, p);

  if (global->initializer)
  {
    ir_print_str(p, " ");

    ir_value_dump_with_type(global->initializer, p);
  }
  else
  {
    ir_print_str(p, " zeroinitializer");
  }

  ir_print_str(p, "\n");
}
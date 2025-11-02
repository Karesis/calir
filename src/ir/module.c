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



#include "ir/module.h"
#include "ir/context.h"
#include "ir/function.h"
#include "ir/global.h"
#include "ir/printer.h"
#include "ir/type.h"
#include "utils/bump.h"
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


  IRModule *mod = BUMP_ALLOC_ZEROED(&ctx->ir_arena, IRModule);
  if (!mod)
  {
    return NULL;
  }


  mod->context = ctx;


  mod->name = ir_context_intern_str(ctx, name);
  if (!mod->name)
  {


    return NULL;
  }



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

  ir_printf(p, "module = \"%s\"\n", mod->name);
  ir_print_str(p, "\n");


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
        ir_printf(p, "%%%s = type ", type->as.aggregate.name);
        ir_print_str(p, "{ ");

        for (size_t i = 0; i < type->as.aggregate.member_count; i++)
        {
          if (i > 0)
          {
            ir_print_str(p, ", ");
          }
          ir_type_dump(type->as.aggregate.member_types[i], p);
        }
        ir_print_str(p, " }\n");
      }
    }
    ir_print_str(p, "\n");
  }


  IDList *global_iter;
  list_for_each(&mod->globals, global_iter)
  {
    IRGlobalVariable *global = list_entry(global_iter, IRGlobalVariable, list_node);
    ir_global_variable_dump(global, p);
  }
  if (!list_empty(&mod->globals))
  {
    ir_print_str(p, "\n");
  }


  IDList *iter_func;
  list_for_each(&mod->functions, iter_func)
  {
    IRFunction *func = list_entry(iter_func, IRFunction, list_node);
    ir_function_dump(func, p);
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
  ir_printer_init_file(&p, stream);
  ir_module_dump_internal(mod, &p);
}

/**
 * @brief [策略 2] 打印到 StringBuf*
 */
const char *
ir_module_dump_to_string(IRModule *mod, Bump *arena)
{

  StringBuf buf;
  string_buf_init(&buf, arena);


  IRPrinter p;
  ir_printer_init_string_buf(&p, &buf);


  ir_module_dump_internal(mod, &p);


  return string_buf_get(&buf);
}
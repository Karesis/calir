// src/ir/module.c
#include "ir/module.h"
#include "ir/context.h"  // 核心依赖
#include "ir/function.h" // 需要 function_dump
#include "ir/global.h"
#include "ir/type.h"
#include "utils/bump.h" // 需要 BUMP_ALLOC 和 BUMP_ALLOC_ZEROED
#include "utils/hashmap.h"

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
 * @brief 将模块的 IR 打印到指定的流 (例如 stdout)
 *
 */
void
ir_module_dump(IRModule *mod, FILE *stream)
{
  if (!mod)
  {
    fprintf(stream, "; <null module>\n");
    return;
  }

  fprintf(stream, "module = \"%s\"\n", mod->name);
  fprintf(stream, "\n");

  // 遍历并打印所有命名结构体
  StrHashMap *struct_cache = mod->context->named_struct_cache;
  if (struct_cache && str_hashmap_size(struct_cache) > 0)
  {
    StrHashMapIter iter = str_hashmap_iter(struct_cache);
    StrHashMapEntry entry;

    while (str_hashmap_iter_next(&iter, &entry))
    {
      IRType *type = (IRType *)entry.value;

      // 确保它是一个正确的命名结构体
      if (type->kind == IR_TYPE_STRUCT && type->as.aggregate.name)
      {
        // 打印: %point = type
        fprintf(stream, "%%%s = type ", type->as.aggregate.name);

        // 打印: { i32, ptr, %other_struct }
        fprintf(stream, "{ ");
        for (size_t i = 0; i < type->as.aggregate.member_count; i++)
        {
          if (i > 0)
          {
            fprintf(stream, ", ");
          }
          // ir_type_dump 会正确打印 i32, ptr, 或 %other_struct
          ir_type_dump(type->as.aggregate.member_types[i], stream);
        }
        fprintf(stream, " }\n");
      }
    }
    fprintf(stream, "\n"); // 在类型和全局变量之间加个空行
  }

  // 打印所有全局变量
  IDList *global_iter;
  list_for_each(&mod->globals, global_iter)
  {
    IRGlobalVariable *global = list_entry(global_iter, IRGlobalVariable, list_node);
    ir_global_variable_dump(global, stream);
  }
  if (!list_empty(&mod->globals))
  {
    fprintf(stream, "\n"); // 在全局变量和函数之间加个空行
  }

  // 打印所有函数
  IDList *iter_func;
  list_for_each(&mod->functions, iter_func)
  {
    IRFunction *func = list_entry(iter_func, IRFunction, list_node);
    ir_function_dump(func, stream);
  }
}
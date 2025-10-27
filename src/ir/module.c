#include "ir/module.h"
#include "ir/context.h"  // <-- [新] 核心依赖
#include "ir/function.h" // 需要 function_dump
#include "ir/global.h"
#include "utils/bump.h" // <-- [新] 需要 BUMP_ALLOC 和 BUMP_ALLOC_ZEROED

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

  // 1. [修改] 从 ir_arena 分配
  IRModule *mod = BUMP_ALLOC_ZEROED(&ctx->ir_arena, IRModule);
  if (!mod)
  {
    return NULL; // OOM
  }

  // 2. [新] 存储 Context 指针
  mod->context = ctx;

  // 3. [修改] 唯一化(Intern)字符串, 而不是 strdup
  mod->name = ir_context_intern_str(ctx, name);
  if (!mod->name)
  {
    // OOM (虽然在 Arena 模型中不太可能发生)
    // 注意：我们无法 "free(mod)"，因为它在 Arena 中
    return NULL;
  }

  // 4. [修改] BUMP_ALLOC_ZEROED 已将 prev/next 设为 NULL
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
    fprintf(stream, "<null module>\n");
    return;
  }

  fprintf(stream, "; ModuleID = '%s'\n", mod->name);
  fprintf(stream, "\n");

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

  // 打印所有函数 (依赖 ir_function_dump)
  IDList *iter_func;
  list_for_each(&mod->functions, iter_func)
  {
    IRFunction *func = list_entry(iter_func, IRFunction, list_node);
    ir_function_dump(func, stream);
    fprintf(stream, "\n");
  }
}
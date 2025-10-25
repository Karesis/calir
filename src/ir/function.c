#include "ir/function.h"
#include "context.h"       // <-- [新]
#include "ir/basicblock.h" // 需要 basic_block_dump
#include "ir/module.h"
#include "ir/type.h"
#include "ir/value.h"
#include "utils/bump.h" // <-- [新]

#include <assert.h>
#include <stdlib.h>
#include <string.h>

// --- IRArgument 实现 ---

IRArgument *
ir_argument_create(IRFunction *func, IRType *type, const char *name)
{
  assert(func != NULL && "Parent function cannot be NULL");
  IRContext *ctx = func->parent->context; // [新] 从父级获取 Context

  // [修改] 从 ir_arena 分配
  IRArgument *arg = BUMP_ALLOC_ZEROED(&ctx->ir_arena, IRArgument);
  if (!arg)
    return NULL;

  arg->parent = func;

  // [修改] 显式初始化链表 (BUMP_ALLOC_ZEROED 不够)
  list_init(&arg->list_node);

  // 初始化 IRValueNode 基类
  arg->value.kind = IR_KIND_ARGUMENT;
  arg->value.name = ir_context_intern_str(ctx, name); // [修改] Intern 名字
  arg->value.type = type;
  list_init(&arg->value.uses); // [修改] 显式初始化

  // 添加到父函数的参数链表
  list_add_tail(&func->arguments, &arg->list_node);

  return arg;
}

// --- IRFunction 实现 ---

IRFunction *
ir_function_create(IRModule *mod, const char *name, IRType *ret_type)
{
  assert(mod != NULL && "Parent module cannot be NULL");
  IRContext *ctx = mod->context; // [新] 从父级获取 Context

  // [修改] 从 ir_arena 分配
  IRFunction *func = BUMP_ALLOC_ZEROED(&ctx->ir_arena, IRFunction);
  if (!func)
    return NULL;

  func->parent = mod;
  func->return_type = ret_type;

  // [修改] 显式初始化链表
  list_init(&func->list_node);
  list_init(&func->arguments);
  list_init(&func->basic_blocks);

  // 初始化 IRValueNode 基类
  func->entry_address.kind = IR_KIND_FUNCTION;
  func->entry_address.name = ir_context_intern_str(ctx, name); // [修改] Intern 名字
  list_init(&func->entry_address.uses);                        // [修改] 显式初始化

  // TODO: [不变] 函数的 'type' 应该是一个 "FunctionType"
  func->entry_address.type = ret_type;

  // 添加到父模块的函数链表
  list_add_tail(&mod->functions, &func->list_node);

  return func;
}

/**
 * @brief ir_function_dump (保持不变)
 */
void
ir_function_dump(IRFunction *func, FILE *stream)
{
  // (此函数的实现保持不变, 它的依赖 (arg_dump, bb_dump)
  // 仍然有效或将被更新)
  if (!func)
  {
    fprintf(stream, "<null function>\n");
    return;
  }

  // 1. 打印函数签名
  char type_str[32];
  ir_type_to_string(func->return_type, type_str, sizeof(type_str));
  fprintf(stream, "define %s @%s(", type_str, func->entry_address.name);

  // 2. 打印参数
  IDList *arg_iter;
  int first_arg = 1;
  list_for_each(&func->arguments, arg_iter)
  {
    if (!first_arg)
    {
      fprintf(stream, ", ");
    }
    IRArgument *arg = list_entry(arg_iter, IRArgument, list_node);
    // (假设 IRArgument 的 name 是 const char*)
    ir_type_to_string(arg->value.type, type_str, sizeof(type_str));
    fprintf(stream, "%s %%%s", type_str, arg->value.name);
    first_arg = 0;
  }

  fprintf(stream, ") {\n");

  // 3. 打印所有基本块
  IDList *bb_iter;
  list_for_each(&func->basic_blocks, bb_iter)
  {
    IRBasicBlock *bb = list_entry(bb_iter, IRBasicBlock, list_node);
    ir_basic_block_dump(bb, stream); // <-- 依赖 basicblock.c
  }

  fprintf(stream, "}\n");
}
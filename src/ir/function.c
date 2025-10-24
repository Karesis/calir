// src/ir/function.c

#include "ir/function.h"
#include "ir/basicblock.h" // <-- 需要 (用于 destroy 和 dump)
#include "ir/constant.h"
#include "ir/module.h"
#include "ir/type.h" // <-- 需要 (用于 dump)
#include "ir/value.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

// --- IRArgument 实现 ---

IRArgument *
ir_argument_create(IRFunction *func, IRType *type, const char *name)
{
  assert(func != NULL && "Parent function cannot be NULL");

  IRArgument *arg = (IRArgument *)malloc(sizeof(IRArgument));
  if (!arg)
    return NULL;

  arg->parent = func;
  list_init(&arg->list_node);

  // 初始化 IRValueNode 基类
  arg->value.kind = IR_KIND_ARGUMENT;
  arg->value.name = strdup(name);
  arg->value.type = type;
  list_init(&arg->value.uses);

  // 添加到父函数的参数链表
  list_add_tail(&func->arguments, &arg->list_node);

  return arg;
}

void
ir_argument_destroy(IRArgument *arg)
{
  if (!arg)
    return;

  // 1. (已解决的 TODO) 替换所有对该参数的使用
  //    在销毁参数之前，将所有对它的使用替换为 'undef' 值
  if (!list_empty(&arg->value.uses))
  {
    IRValueNode *undef_val = ir_constant_get_undef(arg->value.type);
    ir_value_replace_all_uses_with(&arg->value, undef_val);
  }
  // 断言检查
  assert(list_empty(&arg->value.uses) && "Argument still in use when destroyed!");

  // 2. 从父链表中移除
  list_del(&arg->list_node);

  // 3. 释放名字
  free(arg->value.name);

  // 4. 释放自身
  free(arg);
}

// --- IRFunction 实现 ---

IRFunction *
ir_function_create(IRModule *mod, const char *name, IRType *ret_type)
{
  assert(mod != NULL && "Parent module cannot be NULL");

  IRFunction *func = (IRFunction *)malloc(sizeof(IRFunction));
  if (!func)
    return NULL;

  func->parent = mod;
  func->return_type = ret_type;
  list_init(&func->list_node);
  list_init(&func->arguments);
  list_init(&func->basic_blocks);

  // 初始化 IRValueNode 基类 (代表函数地址)
  func->entry_address.kind = IR_KIND_FUNCTION;
  func->entry_address.name = strdup(name);
  list_init(&func->entry_address.uses);

  // TODO: 函数的 'type' 应该是一个 "FunctionType"，
  // 它包含返回类型和参数类型。
  // 目前类型系统还不支持，暂时用 return_type 代替。
  func->entry_address.type = ret_type;

  // 添加到父模块的函数链表
  list_add_tail(&mod->functions, &func->list_node);

  return func;
}

void
ir_function_destroy(IRFunction *func)
{
  if (!func)
    return;

  // 1. 从父模块链表中移除
  list_del(&func->list_node);

  // 2. 销毁所有参数
  IDList *iter, *temp;
  list_for_each_safe(&func->arguments, iter, temp)
  {
    IRArgument *arg = list_entry(iter, IRArgument, list_node);
    ir_argument_destroy(arg);
  }

  // 3. 销毁所有基本块
  // (这需要 ir_basic_block_destroy 的实现)
  list_for_each_safe(&func->basic_blocks, iter, temp)
  {
    IRBasicBlock *bb = list_entry(iter, IRBasicBlock, list_node);
    ir_basic_block_destroy(bb); // <-- 依赖 basicblock.c
  }

  // 4. 释放函数名 (在 ValueNode 中)
  free(func->entry_address.name);

  // 5. 释放函数自身
  free(func);
}

void
ir_function_dump(IRFunction *func, FILE *stream)
{
  if (!func)
  {
    fprintf(stream, "<null function>\n");
    return;
  }

  // 1. 打印函数签名 (e.g., "define i32 @main")
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
    ir_type_to_string(arg->value.type, type_str, sizeof(type_str));
    fprintf(stream, "%s %%%s", type_str, arg->value.name);
    first_arg = 0;
  }

  fprintf(stream, ") {\n");

  // 3. 打印所有基本块
  // (这需要 ir_basic_block_dump 的实现)
  IDList *bb_iter;
  list_for_each(&func->basic_blocks, bb_iter)
  {
    IRBasicBlock *bb = list_entry(bb_iter, IRBasicBlock, list_node);
    ir_basic_block_dump(bb, stream); // <-- 依赖 basicblock.c
  }

  fprintf(stream, "}\n");
}
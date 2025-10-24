// src/ir/module.c

#include "ir/module.h"

// 我们需要包含 function.h 来遍历和销毁函数
// (你也需要为你尚未定义的 global_variable.h 做同样的事情)
#include "ir/function.h"
// #include "ir/global_variable.h" // <-- 将来需要

#include <assert.h> // for assert
#include <stdlib.h> // for malloc, free
#include <string.h> // for strdup

/**
 * @brief 创建一个新模块 (Module)
 */
IRModule *
ir_module_create(const char *name)
{
  IRModule *mod = (IRModule *)malloc(sizeof(IRModule));
  if (!mod)
  {
    return NULL; // 内存分配失败
  }

  // 复制 name 字符串，这样模块就拥有了它
  mod->name = strdup(name);
  if (!mod->name)
  {
    free(mod);
    return NULL; // strdup 失败
  }

  // 初始化两个链表头 (设置 prev/next 指向自己)
  list_init(&mod->functions);
  list_init(&mod->globals);

  return mod;
}

/**
 * @brief 销毁一个模块
 */
void
ir_module_destroy(IRModule *mod)
{
  if (!mod)
  {
    return;
  }

  IDList *iter, *temp;

  // 1. 销毁所有函数
  // (需要 ir_function_destroy 的实现)
  list_for_each_safe(&mod->functions, iter, temp)
  {
    // 1. 从 iter (IDList*) 获取容器 IRFunction*
    IRFunction *func = list_entry(iter, IRFunction, list_node);

    // 2. 递归销毁该函数
    ir_function_destroy(func);
    // (ir_function_destroy 内部会负责 list_del(iter) 和 free(func))
  }

  // 2. 销毁所有全局变量
  // (你需要实现 IRGlobalVariable 和 ir_global_variable_destroy)
  /*
  list_for_each_safe(&mod->globals, iter, temp)
  {
      IRGlobalVariable *global = list_entry(iter, IRGlobalVariable, list_node);
      ir_global_variable_destroy(global);
  }
  */

  // 3. 释放 name 字符串
  free(mod->name);

  // 4. 释放模块结构体本身
  free(mod);
}

/**
 * @brief 将模块的 IR 打印到指定的流 (例如 stdout)
 */
void
ir_module_dump(IRModule *mod, FILE *stream)
{
  if (!mod)
  {
    fprintf(stream, "<null module>\n");
    return;
  }

  // 打印模块头 (类似 LLVM IR)
  fprintf(stream, "; ModuleID = '%s'\n", mod->name);
  // 你也可以在这里打印 target triple 和 datalayout (如果以后添加的话)
  fprintf(stream, "\n");

  // 1. 打印所有全局变量
  // (你需要实现 ir_global_variable_dump)
  /*
  IDList *iter_global;
  list_for_each(&mod->globals, iter_global)
  {
      IRGlobalVariable *global = list_entry(iter_global, IRGlobalVariable, list_node);
      ir_global_variable_dump(global, stream); // <-- 需要实现
      fprintf(stream, "\n");
  }
  */
  if (!list_empty(&mod->globals))
  {
    fprintf(stream, "; (Global variables not yet printable)\n\n");
  }

  // 2. 打印所有函数
  // (需要 ir_function_dump 的实现)
  IDList *iter_func;
  list_for_each(&mod->functions, iter_func)
  {
    IRFunction *func = list_entry(iter_func, IRFunction, list_node);
    ir_function_dump(func, stream); // <-- 下一步需要实现
    fprintf(stream, "\n");
  }
}
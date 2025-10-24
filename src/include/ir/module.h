#ifndef MODULE_H
#define MODULE_H

#include "ir/id_list.h"
#include <stdio.h>

// 模块 (翻译单元)
typedef struct
{
  char *name;
  IDList functions; // 链表头 (元素是 IRFunction)
  IDList globals;   // 链表头 (元素是 IRGlobalVariable)
} IRModule;

/**
 * @brief 创建一个新模块 (Module)
 * @param name 模块的名称 (将被复制)
 * @return 指向新模块的指针
 */
IRModule *ir_module_create(const char *name);

/**
 * @brief 销毁一个模块
 * * 这将递归销毁所有包含的函数、全局变量等。
 * @param mod 要销毁的模块
 */
void ir_module_destroy(IRModule *mod);

/**
 * @brief 将模块的 IR 打印到指定的流 (例如 stdout)
 * * @param mod 要打印的模块
 * @param stream 输出流
 */
void ir_module_dump(IRModule *mod, FILE *stream);

#endif
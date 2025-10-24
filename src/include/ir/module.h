#ifndef MODULE_H
#define MODULE_H

#include "ir/id_list.h"

// 模块 (翻译单元)
typedef struct
{
  const char *name;
  IDList functions; // 链表头 (元素是 IRFunction)
  IDList globals;   // 链表头 (元素是 IRGlobalVariable)
} IRModule;

IRModule *ir_module_create(const char *name);

#endif
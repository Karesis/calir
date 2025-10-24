#ifndef FUNCTION_H
#define FUNCTION_H

#include "ir/id_list.h"
#include "ir/module.h"
#include "ir/type.h"
#include "ir/value.h"

// 函数
typedef struct
{
  IRValueNode entry_address; // 函数入口地址
  IDList list_node;          // <-- 节点，用于加入 Module->functions 链表

  IRType *return_type;
  IDList arguments;    // 链表头 (元素是 IRArgument)
  IDList basic_blocks; // 链表头 (元素是 IRBasicBlock)
  IRModule *parent;
} IRFunction;

// 函数参数
typedef struct
{
  IRValueNode value;
  IDList list_node; // <-- 节点，用于加入 Function->arguments 链表
  IRFunction *parent;
} IRArgument;

IRFunction *ir_function_create(IRModule *mod, const char *name, IRType *ret_type);
IRArgument *ir_argument_create(IRFunction *func, IRType *type, const char *name);
#endif
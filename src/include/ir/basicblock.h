#ifndef BASIC_BLOCK_H
#define BASIC_BLOCK_H

#include "ir/function.h"
#include "ir/id_list.h"
#include "ir/value.h"

// 基本块
typedef struct
{
  IRValueNode label_address; // 标签地址
  IDList list_node;          // <-- 节点，用于加入 Function->basic_blocks 链表

  IDList instructions; // 链表头 (元素是 IRInstruction)
  IRFunction *parent;
} IRBasicBlock;

IRBasicBlock *ir_basic_block_create(IRFunction *func, const char *name);

#endif
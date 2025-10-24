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

/**
 * @brief 创建一个新基本块，并将其附加到函数末尾
 * @param func 父函数
 * @param name 基本块的标签名 (将被复制)
 * @return 指向新基本块的指针
 */
IRBasicBlock *ir_basic_block_create(IRFunction *func, const char *name);

/**
 * @brief 销毁一个基本块 (及其所有指令)
 */
void ir_basic_block_destroy(IRBasicBlock *bb);

// --- 调试 ---

/**
 * @brief 将基本块的 IR (及其所有指令) 打印到流
 * @param bb 要打印的基本块
 * @param stream 输出流
 */
void ir_basic_block_dump(IRBasicBlock *bb, FILE *stream);

#endif

#ifndef BASIC_BLOCK_H
#define BASIC_BLOCK_H

#include "ir/function.h"
#include "ir/printer.h"
#include "ir/value.h"
#include "utils/id_list.h"

/**
 * @brief 基本块
 */
typedef struct IRBasicBlock
{
  IRValueNode label_address; // 标签地址
  IDList list_node;          // <-- 节点，用于加入 Function->basic_blocks 链表

  IDList instructions; // 链表头 (元素是 IRInstruction)
  IRFunction *parent;  // <-- 指向父函数
} IRBasicBlock;

/**
 * @brief 创建一个新基本块 (在 Arena 中)
 * @param func 父函数 (用于获取 Context 和添加到链表)
 * @param name 基本块的标签名 (将被 intern)
 * @return 指向新基本块的指针
 */
IRBasicBlock *ir_basic_block_create(IRFunction *func, const char *name);

/**
 * @brief 将一个基本块附加到其父函数的末尾
 * @param func 父函数
 * @param bb 要附加的基本块
 */
void ir_function_append_basic_block(IRFunction *func, IRBasicBlock *bb);

// --- 调试 ---
/**
 * @brief 将单个基本块的 IR 打印到 IRPrinter
 * [!!] 签名已更改
 * @param bb 要打印的基本块
 * @param p 打印机 (策略)
 */
void ir_basic_block_dump(IRBasicBlock *bb, IRPrinter *p);

#endif
#ifndef INSTRUCTION_H
#define INSTRUCTION_H

#include "ir/basicblock.h"
#include "ir/value.h"
#include "utils/id_list.h"
#include <stdio.h>

// 指令类型
typedef enum
{
  // 终结者指令
  IR_OP_RET, // return <val>
  IR_OP_BR,  // branch <target_bb>

  // 二元运算
  IR_OP_ADD, // add <type> <op1>, <op2>
  IR_OP_SUB, // sub <type> <op1>, <op2>

  // 内存操作
  IR_OP_ALLOCA,
  IR_OP_LOAD,
  IR_OP_STORE,
} IROpcode;

// 指令
typedef struct
{
  IRValueNode result; // 指令计算的结果
  IDList list_node;   // <-- 节点，用于加入 BasicBlock->instructions 链表

  IROpcode opcode;
  IDList operands; // 所使用的所有节点
  IRBasicBlock *parent;
} IRInstruction;

/**
 * @brief 销毁一条指令
 * * 这将自动解除 (unlink) 并销毁 (destroy) 它所有的 Use 边
 * (即它使用的所有 operands)。
 * @param inst 要销毁的指令
 */
void ir_instruction_destroy(IRInstruction *inst);

// --- 调试 ---

/**
 * @brief 将单条指令的 IR 打印到流
 * @param inst 要打印的指令
 * @param stream 输出流
 */
void ir_instruction_dump(IRInstruction *inst, FILE *stream);

#endif
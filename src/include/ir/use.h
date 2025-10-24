#ifndef USE_H
#define USE_H

#include "ir/basicblock.h"
#include "ir/id_list.h"
#include "ir/value.h"

// 指令类型
typedef enum
{
  // 终结者指令
  IR_OP_RET, // return <val>
  IR_OP_BR,  // branch <target_bb>

  // 二元运算
  IR_OP_ADD, // add <type> <op1>, <op2>
  IR_OP_SUB, // sub <type> <op1>, <op2>
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

#endif
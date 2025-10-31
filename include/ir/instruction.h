#ifndef INSTRUCTION_H
#define INSTRUCTION_H

#include "ir/basicblock.h"
#include "ir/printer.h"
#include "ir/value.h"
#include "utils/id_list.h"

// 指令类型
typedef enum
{
  // 终结者指令
  IR_OP_RET,     // return <val>
  IR_OP_BR,      // branch <target_bb>
  IR_OP_COND_BR, // branch <cond>, <true_bb>, <false_bb>

  // 二元运算
  IR_OP_ADD,  // add <type> <op1>, <op2>
  IR_OP_SUB,  // sub <type> <op1>, <op2>
  IR_OP_ICMP, // icmp <pred> <type> <op1>, <op2>

  // 内存操作
  IR_OP_ALLOCA,
  IR_OP_LOAD,
  IR_OP_STORE,
  IR_OP_PHI, // phi <type> [ <val1>, <bb1> ], [ <val2>, <bb2> ], ...
  IR_OP_GEP, // gep
  IR_OP_CALL // call
} IROpcode;

// ICMP (Integer Compare) 谓词
typedef enum
{
  IR_ICMP_EQ,  // 'eq' (equal)
  IR_ICMP_NE,  // 'ne' (not equal)
  IR_ICMP_UGT, // 'ugt' (unsigned greater than)
  IR_ICMP_UGE, // 'uge' (unsigned greater or equal)
  IR_ICMP_ULT, // 'ult' (unsigned less than)
  IR_ICMP_ULE, // 'ule' (unsigned less or equal)
  IR_ICMP_SGT, // 'sgt' (signed greater than)
  IR_ICMP_SGE, // 'sge' (signed greater or equal)
  IR_ICMP_SLT, // 'slt' (signed less than)
  IR_ICMP_SLE, // 'sle' (signed less or equal)
} IRICmpPredicate;

// 指令
typedef struct
{
  IRValueNode result; // 指令计算的结果
  IDList list_node;   // <-- 节点，用于加入 BasicBlock->instructions 链表

  IROpcode opcode;
  IDList operands; // 所使用的所有节点
  IRBasicBlock *parent;
  union {
    // 对应 IR_OP_ICMP
    struct
    {
      IRICmpPredicate predicate;
    } icmp;
    // 对应 IR_OP_GEP
    struct
    {
      IRType *source_type; // GEP 'inbounds' <type>, <ptr> ...
      bool inbounds;
    } gep;
  } as;
} IRInstruction;

/**
 * @brief 从其父基本块中安全地擦除一条指令
 */
void ir_instruction_erase_from_parent(IRInstruction *inst);

// --- 调试 ---

/**
 * @brief 将单条指令的 IR 打印到 IRPrinter
 * [!!] 签名已更改
 * @param inst 要打印的指令
 * @param p 打印机 (策略)
 */
void ir_instruction_dump(IRInstruction *inst, IRPrinter *p);

#endif
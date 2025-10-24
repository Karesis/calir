#include "builder.h"
#include <assert.h> // for assert
#include <stdlib.h> // for malloc

// --- Builder API 实现 ---

IRValueNode *
ir_build_add(IRBuilder *builder, IRValueNode *lhs, IRValueNode *rhs, const char *name)
{
  assert(builder->insert_point != NULL && "Builder insert point is not set!");
  assert(lhs->type == rhs->type && "ADD operands must have the same type!");

  // 1. 分配和初始化 Instruction
  IRInstruction *inst = (IRInstruction *)malloc(sizeof(IRInstruction));

  inst->opcode = IR_OP_ADD;
  inst->parent = builder->insert_point;

  // 2. 初始化它自己的 ValueNode (result)
  inst->result.kind = IR_KIND_INSTRUCTION;
  inst->result.type = lhs->type; // 'add' 的结果类型与操作数相同
  inst->result.name = name;
  list_init(&inst->result.uses); // 刚创建，还没有人使用它

  // 3. 初始化它自己的链表节点
  list_init(&inst->list_node);
  list_init(&inst->operands); // 刚创建，还没有操作数

  // 4. 添加操作数 (!! 关键 !!)
  ir_instruction_add_operand(inst, lhs);
  ir_instruction_add_operand(inst, rhs);

  // 5. 将指令插入到基本块
  list_add_tail(&builder->insert_point->instructions, &inst->list_node);

  // 6. 返回它的 "Value"
  return (IRValueNode *)inst; // 因为 result 是第一个成员，可以安全转换
}

// ... ir_build_ret 和其他函数的实现 ...
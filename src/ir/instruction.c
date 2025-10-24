#include "ir.h"

// --- 内部辅助函数 (这个是关键) ---
// (你可能想把它放在一个单独的 ir/use.c 或 ir/instruction.c 里)
void
ir_instruction_add_operand(IRInstruction *user, IRValueNode *val)
{
  // 1. 创建 Use 边
  IRUse *use = (IRUse *)malloc(sizeof(IRUse));
  use->value = val;
  use->user = user;

  // 2. 把它挂载到 "User" (Instruction) 的 operands 链表
  list_add_tail(&user->operands, &use->operands_node);

  // 3. 把它挂载到 "Value" (ValueNode) 的 uses 链表
  list_add_tail(&val->uses, &use->uses_node);
}
#include "ir/instruction.h"
#include "ir/basicblock.h" // 需要 BasicBlock->parent
#include "ir/constant.h"   // 需要 ir_constant_get_undef
#include "ir/context.h"    // 需要 Context
#include "ir/function.h"   // 需要 Function->parent
#include "ir/module.h"     // 需要 Module->context
#include "ir/type.h"       // 需要 ir_type_dump
#include "ir/use.h"        // 需要 ir_use_unlink
#include "ir/value.h"      // 需要 ir_value_dump, ir_value_replace_all_uses_with

#include <assert.h>
#include <stdlib.h>
#include <string.h>

// --- 内部辅助函数 (用于 dump) ---

/**
 * @brief (内部) 获取第 N 个操作数 (实现不变)
 */
static IRValueNode *
get_operand(IRInstruction *inst, int index)
{
  IDList *head = &inst->operands;
  IDList *iter = head->next;
  while (index > 0 && iter != head)
  {
    iter = iter->next;
    index--;
  }

  if (iter == head)
    return NULL;

  IRUse *use = list_entry(iter, IRUse, user_node);
  return use->value;
}

// --- 生命周期 (新) ---

/**
 * @brief [新] 从其父基本块中安全地擦除一条指令
 */
void
ir_instruction_erase_from_parent(IRInstruction *inst)
{
  if (!inst)
    return;

  // 1. [新] 获取 Context
  //    inst -> BasicBlock -> Function -> Module -> Context
  assert(inst->parent != NULL && "Instruction has no parent BasicBlock");
  assert(inst->parent->parent != NULL && "BasicBlock has no parent Function");
  assert(inst->parent->parent->parent != NULL && "Function has no parent Module");
  IRContext *ctx = inst->parent->parent->parent->context;

  // 2. [修改] 将所有对该指令结果的使用替换为 'undef'
  if (inst->result.type->kind != IR_TYPE_VOID && !list_empty(&inst->result.uses))
  {
    // [修改] 调用新的 Context API
    IRValueNode *undef = ir_constant_get_undef(ctx, inst->result.type);
    ir_value_replace_all_uses_with(&inst->result, undef);
  }
  assert(list_empty(&inst->result.uses) && "Instruction result still in use!");

  // 3. [修改] 解开 (unlink) 所有 Operands (IRUse 边)
  //    我们不再 "destroy" use, 只是 "unlink"
  IDList *iter, *temp;
  list_for_each_safe(&inst->operands, iter, temp)
  {
    IRUse *use = list_entry(iter, IRUse, user_node);
    ir_use_unlink(use);
    // (IRUse 对象仍在 Arena 中, 将被统一释放)
  }

  // 4. 从父基本块的链表中移除
  list_del(&inst->list_node);
}

// --- 调试 ---

void
ir_instruction_dump(IRInstruction *inst, FILE *stream)
{
  if (!inst)
  {
    fprintf(stream, "<null instruction>");
    return;
  }

  // 1. 打印结果
  int has_result = (inst->result.type && inst->result.type->kind != IR_TYPE_VOID);
  if (has_result)
  {
    // [修改] inst->result.name 现在是 const char* (interned)
    fprintf(stream, "%%%s = ", inst->result.name);
  }

  // 2. 打印 Opcode 和 Operands
  IRValueNode *op1, *op2;

  switch (inst->opcode)
  {
  case IR_OP_RET:
    fprintf(stream, "ret ");
    op1 = get_operand(inst, 0);
    if (op1)
    {
      ir_value_dump(op1, stream);
    }
    else
    {
      fprintf(stream, "void");
    }
    break;

  case IR_OP_BR:
    fprintf(stream, "br ");
    op1 = get_operand(inst, 0);
    assert(op1 && "br must have a target");
    ir_value_dump(op1, stream);
    break;

  case IR_OP_ADD:
  case IR_OP_SUB:
    fprintf(stream, (inst->opcode == IR_OP_ADD) ? "add " : "sub ");
    op1 = get_operand(inst, 0);
    op2 = get_operand(inst, 1);
    assert(op1 && op2 && "Binary operator needs two operands");

    ir_type_dump(op1->type, stream);
    fprintf(stream, " ");
    ir_value_dump(op1, stream);
    fprintf(stream, ", ");
    ir_value_dump(op2, stream);
    break;

  case IR_OP_ALLOCA:
    fprintf(stream, "alloca ");
    assert(inst->result.type->kind == IR_TYPE_PTR);
    ir_type_dump(inst->result.type->pointee_type, stream);
    break;

  case IR_OP_LOAD:
    fprintf(stream, "load ");
    op1 = get_operand(inst, 0); // 指针
    assert(op1 && "load needs a pointer operand");

    ir_type_dump(inst->result.type, stream);
    fprintf(stream, ", ");
    ir_value_dump(op1, stream);
    break;

  case IR_OP_STORE:
    fprintf(stream, "store ");
    op1 = get_operand(inst, 0); // 要存储的值
    op2 = get_operand(inst, 1); // 目标指针
    assert(op1 && op2 && "store needs value and pointer operands");

    ir_value_dump(op1, stream);
    fprintf(stream, ", ");
    ir_value_dump(op2, stream);
    break;

  default:
    fprintf(stream, "<?? opcode %d>", inst->opcode);
    break;
  }
}
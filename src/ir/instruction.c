// src/ir/instruction.c
#include "ir/instruction.h"
#include "ir/basicblock.h" // 需要 BasicBlock->parent
#include "ir/constant.h"   // 需要 ir_constant_get_undef
#include "ir/context.h"    // 需要 Context
#include "ir/function.h"   // 需要 Function->parent
#include "ir/module.h"     // 需要 Module->context
#include "ir/printer.h"
#include "ir/type.h"  // 需要 ir_type_dump
#include "ir/use.h"   // 需要 ir_use_unlink
#include "ir/value.h" // 需要 ir_value_dump, ir_value_replace_all_uses_with

#include <assert.h>
#include <stdlib.h>
#include <string.h>

// --- 内部辅助函数 (用于 dump) ---

// 辅助函数：将 ICMP 谓词转换为字符串
static const char *
ir_icmp_predicate_to_string(IRICmpPredicate pred)
{
  switch (pred)
  {
  case IR_ICMP_EQ:
    return "eq";
  case IR_ICMP_NE:
    return "ne";
  case IR_ICMP_UGT:
    return "ugt";
  case IR_ICMP_UGE:
    return "uge";
  case IR_ICMP_ULT:
    return "ult";
  case IR_ICMP_ULE:
    return "ule";
  case IR_ICMP_SGT:
    return "sgt";
  case IR_ICMP_SGE:
    return "sge";
  case IR_ICMP_SLT:
    return "slt";
  case IR_ICMP_SLE:
    return "sle";
  default:
    return "??";
  }
}

/**
 * @brief (内部) 获取第 N 个操作数
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

// --- 生命周期  ---

/**
 * @brief 从其父基本块中安全地擦除一条指令
 */
void
ir_instruction_erase_from_parent(IRInstruction *inst)
{
  if (!inst)
    return;

  // 1. 获取 Context
  //    inst -> BasicBlock -> Function -> Module -> Context
  assert(inst->parent != NULL && "Instruction has no parent BasicBlock");
  assert(inst->parent->parent != NULL && "BasicBlock has no parent Function");
  assert(inst->parent->parent->parent != NULL && "Function has no parent Module");
  IRContext *ctx = inst->parent->parent->parent->context;

  // 2. 将所有对该指令结果的使用替换为 'undef'
  if (inst->result.type->kind != IR_TYPE_VOID && !list_empty(&inst->result.uses))
  {
    //  调用 Context API
    IRValueNode *undef = ir_constant_get_undef(ctx, inst->result.type);
    ir_value_replace_all_uses_with(&inst->result, undef);
  }
  assert(list_empty(&inst->result.uses) && "Instruction result still in use!");

  // 3. 解开 (unlink) 所有 Operands (IRUse 边)
  //    不 "destroy" use, 只是 "unlink"
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
ir_instruction_dump(IRInstruction *inst, IRPrinter *p)
{
  if (!inst)
  {
    ir_print_str(p, "<null instruction>");
    return;
  }

  // --- 1. 打印结果 (新规范: %name: type =) ---
  int has_result = (inst->result.type && inst->result.type->kind != IR_TYPE_VOID);
  if (has_result)
  {
    // [!!] 调用已重构的 ir_value_dump_with_type
    ir_value_dump_with_type(&inst->result, p);
    ir_print_str(p, " = ");
  }

  // 2. 打印 Opcode 和 Operands
  IRValueNode *op1, *op2;

  switch (inst->opcode)
  {
  case IR_OP_RET:
    ir_print_str(p, "ret ");
    op1 = get_operand(inst, 0);
    if (op1)
    {
      ir_value_dump_with_type(op1, p);
    }
    else
    {
      ir_print_str(p, "void");
    }
    break;

  case IR_OP_BR:
    ir_print_str(p, "br ");
    op1 = get_operand(inst, 0);
    assert(op1 && "br must have a target");
    ir_value_dump_with_type(op1, p);
    break;

  case IR_OP_COND_BR:
    ir_print_str(p, "br ");
    IRValueNode *cond = get_operand(inst, 0);
    IRValueNode *true_bb = get_operand(inst, 1);
    IRValueNode *false_bb = get_operand(inst, 2);
    assert(cond && true_bb && false_bb && "cond br needs 3 operands");

    ir_value_dump_with_type(cond, p);
    ir_print_str(p, ", ");
    ir_value_dump_with_type(true_bb, p);
    ir_print_str(p, ", ");
    ir_value_dump_with_type(false_bb, p);
    break;

  case IR_OP_ADD:
  case IR_OP_SUB:
    ir_print_str(p, (inst->opcode == IR_OP_ADD) ? "add " : "sub ");
    op1 = get_operand(inst, 0);
    op2 = get_operand(inst, 1);
    assert(op1 && op2 && "Binary operator needs two operands");

    ir_value_dump_with_type(op1, p);
    ir_print_str(p, ", ");
    ir_value_dump_with_type(op2, p);
    break;

  case IR_OP_ALLOCA:
    ir_print_str(p, "alloc ");
    assert(inst->result.type->kind == IR_TYPE_PTR);
    // [!!] 调用已重构的 ir_type_dump
    ir_type_dump(inst->result.type->as.pointee_type, p);
    break;

  case IR_OP_LOAD:
    ir_print_str(p, "load ");
    op1 = get_operand(inst, 0);
    assert(op1 && "load needs a pointer operand");
    ir_value_dump_with_type(op1, p);
    break;

  case IR_OP_STORE:
    ir_print_str(p, "store ");
    op1 = get_operand(inst, 0);
    op2 = get_operand(inst, 1);
    assert(op1 && op2 && "store needs value and pointer operands");

    ir_value_dump_with_type(op1, p);
    ir_print_str(p, ", ");
    ir_value_dump_with_type(op2, p);
    break;

  case IR_OP_ICMP:
    op1 = get_operand(inst, 0);
    op2 = get_operand(inst, 1);
    assert(op1 && op2 && "icmp needs two operands");

    ir_printf(p, "icmp %s ", ir_icmp_predicate_to_string(inst->as.icmp.predicate));
    ir_value_dump_with_type(op1, p);
    ir_print_str(p, ", ");
    ir_value_dump_with_type(op2, p);
    break;

  case IR_OP_PHI:
    ir_print_str(p, "phi ");
    IDList *head = &inst->operands;
    IDList *iter = head->next;
    int i = 0;
    while (iter != head)
    {
      if (i > 0)
      {
        ir_print_str(p, ", ");
      }
      IRUse *val_use = list_entry(iter, IRUse, user_node);
      IRValueNode *val = val_use->value;
      iter = iter->next;
      assert(iter != head && "PHI node must have [val, bb] pairs");
      IRUse *bb_use = list_entry(iter, IRUse, user_node);
      IRValueNode *bb = bb_use->value;

      ir_print_str(p, "[ ");
      ir_value_dump_with_type(val, p);
      ir_print_str(p, ", ");
      ir_value_dump_with_type(bb, p);
      ir_print_str(p, " ]");

      iter = iter->next;
      i++;
    }
    break;

  case IR_OP_GEP:
    ir_print_str(p, "gep ");
    if (inst->as.gep.inbounds)
    {
      ir_print_str(p, "inbounds ");
    }

    IDList *ghead = &inst->operands;
    IDList *giter = ghead->next;
    while (giter != ghead)
    {
      IRUse *use = list_entry(giter, IRUse, user_node);
      ir_value_dump_with_type(use->value, p);

      giter = giter->next;
      if (giter != ghead)
      {
        ir_print_str(p, ", ");
      }
    }
    break;

  case IR_OP_CALL: {
    ir_print_str(p, "call ");
    IRValueNode *callee = get_operand(inst, 0);
    assert(callee != NULL && "call must have a callee");

    ir_type_dump(callee->type, p);
    ir_print_str(p, " ");
    ir_value_dump_name(callee, p);
    ir_print_str(p, "(");

    IDList *chead = &inst->operands;
    IDList *citer = chead->next->next; // 跳过 callee
    int arg_idx = 0;
    while (citer != chead)
    {
      if (arg_idx > 0)
      {
        ir_print_str(p, ", ");
      }
      IRUse *use = list_entry(citer, IRUse, user_node);
      ir_value_dump_with_type(use->value, p);

      citer = citer->next;
      arg_idx++;
    }
    ir_print_str(p, ")");
    break;
  }

  default:
    ir_printf(p, "<?? opcode %d>", inst->opcode);
    break;
  }
}
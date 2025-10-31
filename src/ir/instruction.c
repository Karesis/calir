// src/ir/instruction.c
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
ir_instruction_dump(IRInstruction *inst, FILE *stream)
{
  if (!inst)
  {
    fprintf(stream, "<null instruction>");
    return;
  }

  // --- [MODIFIED] 1. 打印结果 (新规范: %name: type =) ---
  int has_result = (inst->result.type && inst->result.type->kind != IR_TYPE_VOID);
  if (has_result)
  {
    // ir_value_dump_with_type 会打印 "%name: type"
    ir_value_dump_with_type(&inst->result, stream);
    fprintf(stream, " = ");
  }

  // 2. 打印 Opcode 和 Operands
  IRValueNode *op1, *op2;

  switch (inst->opcode)
  {
  case IR_OP_RET:
    fprintf(stream, "ret ");
    op1 = get_operand(inst, 0); // 可能是 NULL (ret void)
    if (op1)
    {
      // [NEW] 打印 "%val: i32"
      ir_value_dump_with_type(op1, stream);
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
    // [NEW] ir_value_dump_with_type 会智能打印 "$label"
    ir_value_dump_with_type(op1, stream);
    break;

  case IR_OP_COND_BR:
    fprintf(stream, "br ");

    IRValueNode *cond = get_operand(inst, 0);
    IRValueNode *true_bb = get_operand(inst, 1);
    IRValueNode *false_bb = get_operand(inst, 2);
    assert(cond && true_bb && false_bb && "cond br needs 3 operands");

    // [NEW] 打印 "br %cmp: i1, $true_bb, $false_bb"
    ir_value_dump_with_type(cond, stream); // 打印 %cmp: i1
    fprintf(stream, ", ");
    ir_value_dump_with_type(true_bb, stream); // 打印 $true_bb
    fprintf(stream, ", ");
    ir_value_dump_with_type(false_bb, stream); // 打印 $false_bb
    break;

  case IR_OP_ADD:
  case IR_OP_SUB:
    fprintf(stream, (inst->opcode == IR_OP_ADD) ? "add " : "sub ");
    op1 = get_operand(inst, 0);
    op2 = get_operand(inst, 1);
    assert(op1 && op2 && "Binary operator needs two operands");

    // [NEW] 打印 "add %a: i32, 20: i32"
    ir_value_dump_with_type(op1, stream);
    fprintf(stream, ", ");
    ir_value_dump_with_type(op2, stream);
    break;

  case IR_OP_ALLOCA:
    fprintf(stream, "alloc ");
    assert(inst->result.type->kind == IR_TYPE_PTR);
    // [OK] 结果 (%p: <i32>) 已由 preamble 打印
    // [OK] ir_type_dump 已被重构, 这里会正确打印 'i32'
    ir_type_dump(inst->result.type->as.pointee_type, stream);
    break;

  case IR_OP_LOAD:
    fprintf(stream, "load ");
    op1 = get_operand(inst, 0); // 指针
    assert(op1 && "load needs a pointer operand");
    ir_value_dump_with_type(op1, stream); // 打印 "%p: <i32>"
    break;

  case IR_OP_STORE:
    fprintf(stream, "store ");
    op1 = get_operand(inst, 0); // 要存储的值
    op2 = get_operand(inst, 1); // 目标指针
    assert(op1 && op2 && "store needs value and pointer operands");

    // [NEW] 打印 "store 10: i32, %p: <i32>"
    ir_value_dump_with_type(op1, stream);
    fprintf(stream, ", ");
    ir_value_dump_with_type(op2, stream);
    break;

  case IR_OP_ICMP:
    const char *pred_str = ir_icmp_predicate_to_string(inst->as.icmp.predicate);
    op1 = get_operand(inst, 0); // lhs
    op2 = get_operand(inst, 1); // rhs
    assert(op1 && op2 && "icmp needs two operands");

    // [!! 已修复 (设计 B) !!]
    // 打印: "icmp slt %a: i32, %b: i32"
    fprintf(stream, "icmp %s ", pred_str);
    ir_value_dump_with_type(op1, stream); // 打印 "%a: i32"
    fprintf(stream, ", ");
    ir_value_dump_with_type(op2, stream); // 打印 "%b: i32"
    break;

  case IR_OP_PHI:
    fprintf(stream, "phi ");
    // [NEW] 结果类型 (%phi.res: i32) 已由 preamble 打印

    IDList *head = &inst->operands;
    IDList *iter = head->next;
    int i = 0;
    while (iter != head)
    {
      if (i > 0)
      {
        fprintf(stream, ", "); // 在条目之间打印逗号
      }

      IRUse *val_use = list_entry(iter, IRUse, user_node);
      IRValueNode *val = val_use->value;

      iter = iter->next; // 移动到下一个
      assert(iter != head && "PHI node must have [val, bb] pairs");
      IRUse *bb_use = list_entry(iter, IRUse, user_node);
      IRValueNode *bb = bb_use->value;

      // [NEW] 打印 "[ %val: i32, $bb_label ]"
      fprintf(stream, "[ ");
      ir_value_dump_with_type(val, stream); // 打印 "%val: i32"
      fprintf(stream, ", ");
      ir_value_dump_with_type(bb, stream); // 打印 "$bb_label"
      fprintf(stream, " ]");

      iter = iter->next; // 移动到下一对
      i++;
    }
    break;

  case IR_OP_GEP:
    fprintf(stream, "gep ");
    if (inst->as.gep.inbounds)
    {
      fprintf(stream, "inbounds ");
    }

    // [NEW] 遍历所有操作数 (base + indices)
    IDList *ghead = &inst->operands;
    IDList *giter = ghead->next;

    while (giter != ghead)
    {
      IRUse *use = list_entry(giter, IRUse, user_node);
      IRValueNode *operand = use->value;

      ir_value_dump_with_type(operand, stream); // 打印 "%p: <%MyStruct>", "0: i32" 等

      giter = giter->next;
      if (giter != ghead)
      {
        fprintf(stream, ", "); // 在索引之间打印逗号
      }
    }
    break;

  case IR_OP_CALL: {
    fprintf(stream, "call ");
    // [NEW] 结果 (%ret: i32) 已由 preamble 打印

    IRValueNode *callee = get_operand(inst, 0);
    assert(callee != NULL && "call must have a callee");

    // [!! 已修复 (Bug B) !!]
    // 添加 "有效冗余" 的函数指针类型
    // 打印: "call <i32(i32)> @my_print(...)"
    ir_type_dump(callee->type, stream); // 打印 "<i32(i32)>"
    fprintf(stream, " ");
    ir_value_dump_name(callee, stream); // 打印 "@my_print"

    fprintf(stream, "(");
    IDList *chead = &inst->operands;
    IDList *citer = chead->next->next; // [!!] 跳过 callee

    int arg_idx = 0;
    while (citer != chead)
    {
      if (arg_idx > 0)
      {
        fprintf(stream, ", ");
      }
      IRUse *use = list_entry(citer, IRUse, user_node);
      // [NEW] 打印 "%arg: i32"
      ir_value_dump_with_type(use->value, stream);

      citer = citer->next;
      arg_idx++;
    }
    fprintf(stream, ")");
    break;
  }

  default:
    fprintf(stream, "<?? opcode %d>", inst->opcode);
    break;
  }
}
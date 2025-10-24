#include "ir/instruction.h"
#include "ir/type.h"  // <-- 依赖, 用于 ir_type_dump
#include "ir/use.h"   // <-- 核心依赖
#include "ir/value.h" // <-- 依赖, 用于 ir_value_dump

#include <assert.h>
#include <stdlib.h>
#include <string.h>

// --- 内部辅助函数 (用于 dump) ---

/**
 * @brief (内部) 获取第 N 个操作数
 * @param inst 指令
 * @param index 操作数索引 (0-based)
 * @return 指向操作数的 IRValueNode, 如果索引越界则返回 NULL
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
    return NULL; // 索引越界

  // 从 IRUse->user_node 获取 IRUse
  IRUse *use = list_entry(iter, IRUse, user_node);
  // 从 IRUse 返回 Value
  return use->value;
}

// --- 生命周期 ---

void
ir_instruction_destroy(IRInstruction *inst)
{
  if (!inst)
    return;

  // 1. 从父基本块的链表中移除
  list_del(&inst->list_node);

  // 2. 销毁所有 Operands (IRUse 边)
  IDList *iter, *temp;
  list_for_each_safe(&inst->operands, iter, temp)
  {
    IRUse *use = list_entry(iter, IRUse, user_node);
    ir_use_unlink(use);
    ir_use_destroy(use);
  }

  // 3. (已解决的 TODO) 处理所有对该指令结果的使用
  //    在销毁指令前, 必须将所有对它结果(inst->result)的使用
  //    替换为一个 'undef' 值。
  if (inst->result.type->kind != IR_TYPE_VOID && !list_empty(&inst->result.uses))
  {
    IRValueNode *undef = ir_constant_get_undef(inst->result.type);
    ir_value_replace_all_uses_with(&inst->result, undef);
  }

  // (断言检查)
  assert(list_empty(&inst->result.uses) && "Instruction result still in use!");

  // 4. 释放指令结果的名字
  free(inst->result.name);

  // 5. 释放指令自身
  free(inst);
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

  // 1. 打印结果 (如果指令有结果的话)
  int has_result = (inst->result.type && inst->result.type->kind != IR_TYPE_VOID);
  if (has_result)
  {
    // 结果值使用 '%'
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
      // e.g., "ret i32 %a"
      ir_value_dump(op1, stream);
    }
    else
    {
      // e.g., "ret void"
      fprintf(stream, "void");
    }
    break;

  case IR_OP_BR:
    fprintf(stream, "br ");
    op1 = get_operand(inst, 0); // 目标基本块
    assert(op1 && "br must have a target");
    // e.g., "br label %entry"
    ir_value_dump(op1, stream);
    break;

  case IR_OP_ADD:
  case IR_OP_SUB:
    fprintf(stream, (inst->opcode == IR_OP_ADD) ? "add " : "sub ");
    op1 = get_operand(inst, 0);
    op2 = get_operand(inst, 1);
    assert(op1 && op2 && "Binary operator needs two operands");

    // e.g., "add i32 %a, %b"
    ir_type_dump(op1->type, stream);
    fprintf(stream, " %%%s, %%%s", op1->name, op2->name);
    break;

  case IR_OP_ALLOCA:
    fprintf(stream, "alloca ");
    // alloca 的结果(inst->result)是一个指针
    // 我们打印该指针指向的类型
    assert(inst->result.type->kind == IR_TYPE_PTR);
    // e.g., "alloca i32"
    ir_type_dump(inst->result.type->pointee_type, stream);
    break;

  case IR_OP_LOAD:
    fprintf(stream, "load ");
    op1 = get_operand(inst, 0); // 指针
    assert(op1 && "load needs a pointer operand");

    // e.g., "load i32, ptr %ptr_a"
    // 1. 打印加载的类型 (即指令结果的类型)
    ir_type_dump(inst->result.type, stream);
    fprintf(stream, ", ");
    // 2. 打印指针操作数 (值和类型)
    ir_value_dump(op1, stream);
    break;

  case IR_OP_STORE:
    fprintf(stream, "store ");
    op1 = get_operand(inst, 0); // 要存储的值
    op2 = get_operand(inst, 1); // 目标指针
    assert(op1 && op2 && "store needs value and pointer operands");

    // e.g., "store i32 %a, ptr %ptr_a"
    // 1. 打印值操作数
    ir_value_dump(op1, stream);
    fprintf(stream, ", ");
    // 2. 打印指针操作数
    ir_value_dump(op2, stream);
    break;

  default:
    fprintf(stream, "<?? opcode %d>", inst->opcode);
    break;
  }
}
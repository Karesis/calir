/*
 * Copyright 2025 Karesis
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "ir/instruction.h"
#include "ir/basicblock.h"
#include "ir/constant.h"
#include "ir/context.h"
#include "ir/function.h"
#include "ir/module.h"
#include "ir/printer.h"
#include "ir/type.h"
#include "ir/use.h"
#include "ir/value.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

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

// [!!] 新增的辅助函数
static const char *
ir_fcmp_predicate_to_string(IRFCmpPredicate pred)
{
  switch (pred)
  {
  case IR_FCMP_OEQ:
    return "oeq";
  case IR_FCMP_OGT:
    return "ogt";
  case IR_FCMP_OGE:
    return "oge";
  case IR_FCMP_OLT:
    return "olt";
  case IR_FCMP_OLE:
    return "ole";
  case IR_FCMP_ONE:
    return "one";
  case IR_FCMP_UEQ:
    return "ueq";
  case IR_FCMP_UGT:
    return "ugt";
  case IR_FCMP_UGE:
    return "uge";
  case IR_FCMP_ULT:
    return "ult";
  case IR_FCMP_ULE:
    return "ule";
  case IR_FCMP_UNE:
    return "une";
  case IR_FCMP_ORD:
    return "ord";
  case IR_FCMP_UNO:
    return "uno";
  case IR_FCMP_TRUE:
    return "true";
  case IR_FCMP_FALSE:
    return "false";
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

/**
 * @brief 从其父基本块中安全地擦除一条指令
 */
void
ir_instruction_erase_from_parent(IRInstruction *inst)
{
  if (!inst)
    return;

  assert(inst->parent != NULL && "Instruction has no parent BasicBlock");
  assert(inst->parent->parent != NULL && "BasicBlock has no parent Function");
  assert(inst->parent->parent->parent != NULL && "Function has no parent Module");
  IRContext *ctx = inst->parent->parent->parent->context;

  if (inst->result.type->kind != IR_TYPE_VOID && !list_empty(&inst->result.uses))
  {

    IRValueNode *undef = ir_constant_get_undef(ctx, inst->result.type);
    ir_value_replace_all_uses_with(&inst->result, undef);
  }
  assert(list_empty(&inst->result.uses) && "Instruction result still in use!");

  IDList *iter, *temp;
  list_for_each_safe(&inst->operands, iter, temp)
  {
    IRUse *use = list_entry(iter, IRUse, user_node);
    ir_use_unlink(use);
  }

  list_del(&inst->list_node);
}

/**
 * @brief 将单条指令的 IR 打印到 IRPrinter
 */
void
ir_instruction_dump(IRInstruction *inst, IRPrinter *p)
{
  if (!inst)
  {
    ir_print_str(p, "<null instruction>");
    return;
  }

  // 1. 打印结果 (e.g., "%res: i32 = ")
  int has_result = (inst->result.type && inst->result.type->kind != IR_TYPE_VOID);
  if (has_result)
  {
    ir_value_dump_with_type(&inst->result, p);
    ir_print_str(p, " = ");
  }

  IRValueNode *op1, *op2, *op3;

  // 2. 打印指令体 (分发)
  switch (inst->opcode)
  {
  /// --- 终止指令 ---
  case IR_OP_RET:
    ir_print_str(p, "ret ");
    op1 = get_operand(inst, 0); // May be NULL for "ret void"
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
    ir_value_dump_with_type(op1, p); // 打印 "$label"
    break;

  case IR_OP_COND_BR:
    ir_print_str(p, "br ");
    op1 = get_operand(inst, 0); /// cond
    op2 = get_operand(inst, 1); /// true_bb
    op3 = get_operand(inst, 2); /// false_bb
    assert(op1 && op2 && op3 && "cond br needs 3 operands");

    ir_value_dump_with_type(op1, p); /// %cond: i1
    ir_print_str(p, ", ");
    ir_value_dump_with_type(op2, p); /// $true_bb
    ir_print_str(p, ", ");
    ir_value_dump_with_type(op3, p); /// $false_bb
    break;

  case IR_OP_SWITCH:
    ir_print_str(p, "switch ");
    op1 = get_operand(inst, 0); /// cond
    op2 = get_operand(inst, 1); /// default_bb
    assert(op1 && op2 && "switch needs at least cond and default");

    ir_value_dump_with_type(op1, p); /// %cond: i32
    ir_print_str(p, ", default ");
    ir_value_dump_with_type(op2, p); /// $default_bb

    ir_print_str(p, " [");

    // 遍历 case, 它们从索引 2 开始，成对出现
    int i = 2;
    IRValueNode *case_val = get_operand(inst, i);
    while (case_val)
    {
      IRValueNode *case_bb = get_operand(inst, i + 1);
      assert(case_bb && "switch case pair is incomplete");

      ir_print_str(p, "\n    ");            // 缩进
      ir_value_dump_with_type(case_val, p); // 10: i32
      ir_print_str(p, ", ");
      ir_value_dump_with_type(case_bb, p); // $case_1

      i += 2;
      case_val = get_operand(inst, i);
    }
    ir_print_str(p, "\n  ]"); // 闭合
    break;

  /// --- 整数二元运算 ---
  case IR_OP_ADD:
    ir_dump_binary_op(inst, p, "add");
    break;
  case IR_OP_SUB:
    ir_dump_binary_op(inst, p, "sub");
    break;
  case IR_OP_MUL:
    ir_dump_binary_op(inst, p, "mul");
    break;
  case IR_OP_UDIV:
    ir_dump_binary_op(inst, p, "udiv");
    break;
  case IR_OP_SDIV:
    ir_dump_binary_op(inst, p, "sdiv");
    break;
  case IR_OP_UREM:
    ir_dump_binary_op(inst, p, "urem");
    break;
  case IR_OP_SREM:
    ir_dump_binary_op(inst, p, "srem");
    break;

  /// --- 浮点二元运算 ---
  case IR_OP_FADD:
    ir_dump_binary_op(inst, p, "fadd");
    break;
  case IR_OP_FSUB:
    ir_dump_binary_op(inst, p, "fsub");
    break;
  case IR_OP_FMUL:
    ir_dump_binary_op(inst, p, "fmul");
    break;
  case IR_OP_FDIV:
    ir_dump_binary_op(inst, p, "fdiv");
    break;

  /// --- 位运算 ---
  case IR_OP_SHL:
    ir_dump_binary_op(inst, p, "shl");
    break;
  case IR_OP_LSHR:
    ir_dump_binary_op(inst, p, "lshr");
    break;
  case IR_OP_ASHR:
    ir_dump_binary_op(inst, p, "ashr");
    break;
  case IR_OP_AND:
    ir_dump_binary_op(inst, p, "and");
    break;
  case IR_OP_OR:
    ir_dump_binary_op(inst, p, "or");
    break;
  case IR_OP_XOR:
    ir_dump_binary_op(inst, p, "xor");
    break;

  /// --- 内存操作 ---
  case IR_OP_ALLOCA:
    ir_print_str(p, "alloc ");
    assert(inst->result.type->kind == IR_TYPE_PTR);
    ir_type_dump(inst->result.type->as.pointee_type, p); // 打印被分配的类型
    break;

  case IR_OP_LOAD:
    ir_print_str(p, "load ");
    op1 = get_operand(inst, 0);
    assert(op1 && "load needs a pointer operand");
    ir_value_dump_with_type(op1, p); // %ptr: <*i32>
    break;

  case IR_OP_STORE:
    ir_print_str(p, "store ");
    op1 = get_operand(inst, 0); // value
    op2 = get_operand(inst, 1); // pointer
    assert(op1 && op2 && "store needs value and pointer operands");
    ir_value_dump_with_type(op1, p); // %val: i32
    ir_print_str(p, ", ");
    ir_value_dump_with_type(op2, p); // %ptr: <*i32>
    break;

  case IR_OP_GEP:
    ir_print_str(p, "gep ");
    if (inst->as.gep.inbounds)
    {
      ir_print_str(p, "inbounds ");
    }

    op1 = get_operand(inst, 0); // base pointer
    assert(op1 && "gep must have at least one operand");
    ir_value_dump_with_type(op1, p);

    // 打印索引
    int gep_idx = 1;
    IRValueNode *idx = get_operand(inst, gep_idx);
    while (idx)
    {
      ir_print_str(p, ", ");
      ir_value_dump_with_type(idx, p);
      idx = get_operand(inst, ++gep_idx);
    }
    break;

  /// --- 比较操作 ---
  case IR_OP_ICMP:
    op1 = get_operand(inst, 0);
    op2 = get_operand(inst, 1);
    assert(op1 && op2 && "icmp needs two operands");
    ir_printf(p, "icmp %s ", ir_icmp_predicate_to_string(inst->as.icmp.predicate));
    ir_value_dump_with_type(op1, p);
    ir_print_str(p, ", ");
    ir_value_dump_with_type(op2, p);
    break;

  case IR_OP_FCMP:
    op1 = get_operand(inst, 0);
    op2 = get_operand(inst, 1);
    assert(op1 && op2 && "fcmp needs two operands");
    ir_printf(p, "fcmp %s ", ir_fcmp_predicate_to_string(inst->as.fcmp.predicate));
    ir_value_dump_with_type(op1, p);
    ir_print_str(p, ", ");
    ir_value_dump_with_type(op2, p);
    break;

  /// --- 类型转换 (Casting) ---
  case IR_OP_TRUNC:
    ir_dump_cast_op(inst, p, "trunc");
    break;
  case IR_OP_ZEXT:
    ir_dump_cast_op(inst, p, "zext");
    break;
  case IR_OP_SEXT:
    ir_dump_cast_op(inst, p, "sext");
    break;
  case IR_OP_FPTRUNC:
    ir_dump_cast_op(inst, p, "fptrunc");
    break;
  case IR_OP_FPEXT:
    ir_dump_cast_op(inst, p, "fpext");
    break;
  case IR_OP_FPTOUI:
    ir_dump_cast_op(inst, p, "fptoui");
    break;
  case IR_OP_FPTOSI:
    ir_dump_cast_op(inst, p, "fptosi");
    break;
  case IR_OP_UITOFP:
    ir_dump_cast_op(inst, p, "uitofp");
    break;
  case IR_OP_SITOFP:
    ir_dump_cast_op(inst, p, "sitofp");
    break;
  case IR_OP_PTRTOINT:
    ir_dump_cast_op(inst, p, "ptrtoint");
    break;
  case IR_OP_INTTOPTR:
    ir_dump_cast_op(inst, p, "inttoptr");
    break;
  case IR_OP_BITCAST:
    ir_dump_cast_op(inst, p, "bitcast");
    break;

  /// --- 其他 ---
  case IR_OP_PHI:
    ir_print_str(p, "phi ");
    int phi_i = 0;
    IRValueNode *phi_val = get_operand(inst, phi_i);
    while (phi_val)
    {
      IRValueNode *phi_bb = get_operand(inst, phi_i + 1);
      assert(phi_bb && "PHI node must have [val, bb] pairs");

      if (phi_i > 0)
      {
        ir_print_str(p, ", ");
      }
      ir_print_str(p, "[ ");
      ir_value_dump_with_type(phi_val, p);
      ir_print_str(p, ", ");
      ir_value_dump_with_type(phi_bb, p);
      ir_print_str(p, " ]");

      phi_i += 2;
      phi_val = get_operand(inst, phi_i);
    }
    break;

  case IR_OP_CALL: {
    ir_print_str(p, "call ");
    op1 = get_operand(inst, 0); // callee
    assert(op1 != NULL && "call must have a callee");

    // 打印函数签名 (callee 的类型)
    assert(op1->type->kind == IR_TYPE_PTR);
    assert(op1->type->as.pointee_type->kind == IR_TYPE_FUNCTION);
    ir_type_dump(op1->type->as.pointee_type, p);

    ir_print_str(p, " ");
    ir_value_dump_name(op1, p); // @func_name
    ir_print_str(p, "(");

    // 打印参数 (从索引 1 开始)
    int arg_idx = 1;
    IRValueNode *arg = get_operand(inst, arg_idx);
    while (arg)
    {
      if (arg_idx > 1)
      {
        ir_print_str(p, ", ");
      }
      ir_value_dump_with_type(arg, p);
      arg = get_operand(inst, ++arg_idx);
    }
    ir_print_str(p, ")");
    break;
  }

  default:
    ir_printf(p, "<?? UNIMPLEMENTED OPCODE %d>", inst->opcode);
    break;
  }
}

/**
 * @brief (内部) 打印二元运算 (e.g., "add %a: i32, %b: i32")
 */
static void
ir_dump_binary_op(IRInstruction *inst, IRPrinter *p, const char *opname)
{
  IRValueNode *op1 = get_operand(inst, 0);
  IRValueNode *op2 = get_operand(inst, 1);
  assert(op1 && op2 && "Binary operator needs two operands");

  ir_print_str(p, opname);
  ir_print_str(p, " ");
  ir_value_dump_with_type(op1, p);
  ir_print_str(p, ", ");
  ir_value_dump_with_type(op2, p);
}

/**
 * @brief (内部) 打印类型转换 (e.g., "zext %a: i32 to i64")
 */
static void
ir_dump_cast_op(IRInstruction *inst, IRPrinter *p, const char *opname)
{
  IRValueNode *op1 = get_operand(inst, 0);
  assert(op1 && "Cast operator needs an operand");

  ir_print_str(p, opname);
  ir_print_str(p, " ");
  ir_value_dump_with_type(op1, p);

  /// 语法: "zext %op: <type> to <result_type>"
  ir_print_str(p, " to ");
  ir_type_dump(inst->result.type, p);
}
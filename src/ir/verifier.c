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

#include "ir/verifier.h"

#include "analysis/cfg.h"
#include "analysis/dom_tree.h"
#include "ir/basicblock.h"
#include "ir/constant.h"
#include "ir/context.h"
#include "ir/function.h"
#include "ir/global.h"
#include "ir/instruction.h"
#include "ir/module.h"
#include "ir/printer.h"
#include "ir/type.h"
#include "ir/use.h"
#include "ir/value.h"
#include "utils/bump.h"
#include "utils/id_list.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>

/*
 * =================================================================
 * --- 辅助宏 ---
 * =================================================================
 */

typedef struct
{
  IRFunction *current_function;
  IRBasicBlock *current_block;
  bool has_error;
  DominatorTree *dom_tree;
  Bump analysis_arena;
  IRPrinter *p;
} VerifierContext;

/**
 * @brief VERIFY_ERROR 的实现
 */
static bool
verify_error_impl(VerifierContext *vctx, IRValueNode *obj, const char *file, int line, const char *fmt, ...)
{
  if (vctx->has_error)
  {
    return false;
  }
  vctx->has_error = true;

  if (!vctx->p)
  {
    return false;
  }

  ir_print_str(vctx->p, "\n--- [CALIR VERIFIER ERROR] ---\n");
  ir_printf(vctx->p, "At:          %s:%d\n", file, line);

  if (vctx->current_function)
  {
    ir_printf(vctx->p, "In Function: %s\n", vctx->current_function->entry_address.name);
  }
  if (vctx->current_block)
  {
    ir_printf(vctx->p, "In Block:    %s\n", vctx->current_block->label_address.name);
  }

  ir_print_str(vctx->p, "Error:       ");
  va_list args;
  va_start(args, fmt);
  vctx->p->append_vfmt_func(vctx->p->target, fmt, args);
  va_end(args);
  ir_print_str(vctx->p, "\n");

  if (obj)
  {
    ir_print_str(vctx->p, "Object:      ");
    if (obj->kind == IR_KIND_INSTRUCTION)
    {
      ir_instruction_dump(container_of(obj, IRInstruction, result), vctx->p);
    }
    else
    {

      ir_value_dump(obj, vctx->p);
    }
    ir_print_str(vctx->p, "\n");
  }

  ir_print_str(vctx->p, "---------------------------------\n");
  return false;
}

/**
 * @brief 调用辅助函数的精简包装器。
 */
#define VERIFY_ERROR(vctx, obj, ...)                                                                                   \
  /* 'return' 会从调用方 (e.g., verify_instruction) 返回 */                                                            \
  return verify_error_impl((vctx), (IRValueNode *)(obj), __FILE__, __LINE__, __VA_ARGS__)

/**
 * @brief 检查一个断言，如果失败则报告错误
 *
 * @param condition 要断言的条件
 * @param vctx Verifier 上下文
 * @param obj 导致错误的对象
 * @param ... 失败时要打印的错误信息
 */
#define VERIFY_ASSERT(condition, vctx, obj, ...)                                                                       \
  do                                                                                                                   \
  {                                                                                                                    \
    if (!(condition))                                                                                                  \
    {                                                                                                                  \
      VERIFY_ERROR(vctx, obj, __VA_ARGS__);                                                                            \
    }                                                                                                                  \
  } while (0)

/*
 * =================================================================
 * --- 内部辅助函数 (基于 id_list.h) ---
 * =================================================================
 */

/**
 * @brief 获取指令的操作数数量
 */
static inline int
get_operand_count(IRInstruction *inst)
{
  int count = 0;
  IDList *iter_node;
  list_for_each(&inst->operands, iter_node)
  {
    count++;
  }
  return count;
}

/**
 * @brief 获取指令的第 N 个操作数 (ValueNode)
 * @param inst 指令
 * @param index 索引
 * @return IRValueNode* (如果越界则返回 NULL)
 */
static inline IRValueNode *
get_operand(IRInstruction *inst, int index)
{
  int i = 0;
  IDList *iter_node;
  list_for_each(&inst->operands, iter_node)
  {
    if (i == index)
    {
      IRUse *use = list_entry(iter_node, IRUse, user_node);
      return use->value;
    }
    i++;
  }
  return NULL;
}

/*
 * =================================================================
 * --- 内部验证函数 ---
 * =================================================================
 */

static bool verify_basic_block(VerifierContext *vctx, IRBasicBlock *bb);
static bool verify_instruction(VerifierContext *vctx, IRInstruction *inst);

/**
 * @brief [!!] (新增) 检查是否为整数类型
 */
static inline bool
ir_type_is_integer(IRType *ty)
{
  return (ty->kind >= IR_TYPE_I1 && ty->kind <= IR_TYPE_I64);
}

/**
 * @brief [!!] (新增) 检查是否为浮点类型
 */
static inline bool
ir_type_is_floating(IRType *ty)
{
  return (ty->kind == IR_TYPE_F32 || ty->kind == IR_TYPE_F64);
}

/**
 * @brief [!!] (新增) 检查是否为指针类型
 */
static inline bool
ir_type_is_pointer(IRType *ty)
{
  return (ty->kind == IR_TYPE_PTR);
}

/**
 * @brief [!!] (新增) 检查是否为终结者指令
 */
static inline bool
ir_is_terminator(IROpcode op)
{
  switch (op)
  {
  case IR_OP_RET:
  case IR_OP_BR:
  case IR_OP_COND_BR:
  case IR_OP_SWITCH: // [!!] 新增
    return true;
  default:
    return false;
  }
}

/**
 * @brief [内部] 检查一个终结者指令是否将 `target_bb` 作为其跳转目标之一。
 *
 * @param term 终结者指令 (必须是 IR_OP_BR 或 IR_OP_COND_BR)
 * @param target_bb 我们正在寻找的目标基本块
 * @return true 如果 'term' 跳转到 'target_bb', 否则 false
 */
static bool
is_terminator_predecessor(IRInstruction *term, IRBasicBlock *target_bb)
{
  IRValueNode *target_val = &target_bb->label_address;

  switch (term->opcode)
  {
  case IR_OP_BR:
    return (get_operand(term, 0) == target_val);

  case IR_OP_COND_BR:
    return (get_operand(term, 1) == target_val || get_operand(term, 2) == target_val);

  case IR_OP_SWITCH: { // [!!] 新增
    /// 检查 default (op 1)
    if (get_operand(term, 1) == target_val)
    {
      return true;
    }
    /// 检查 cases (op 3, 5, 7...)
    int op_count = get_operand_count(term);
    for (int i = 3; i < op_count; i += 2)
    {
      if (get_operand(term, i) == target_val)
      {
        return true;
      }
    }
    return false;
  }
  default:
    return false;
  }
}

/**
 * @brief [内部] 检查 'pred_bb' 是否在 'phi_inst' 的传入块列表中。
 *
 * @param phi_inst PHI 指令
 * @param pred_bb 我们要查找的前驱基本块
 * @return true 如果在列表中找到, 否则 false
 */
static bool
find_in_phi(IRInstruction *phi_inst, IRBasicBlock *pred_bb)
{
  IRValueNode *pred_val = &pred_bb->label_address;
  int count = get_operand_count(phi_inst);

  for (int i = 1; i < count; i += 2)
  {
    if (get_operand(phi_inst, i) == pred_val)
    {
      return true;
    }
  }
  return false;
}

/*
 * =================================================================
 * --- 验证辅助函数  ---
 * =================================================================
 */

/**
 * @brief [!!] (Refactored) 验证终结者指令
 */
static bool
verify_op_terminator(VerifierContext *vctx, IRInstruction *inst)
{
  IRValueNode *value = &inst->result;
  IRFunction *func = vctx->current_function;
  int op_count = get_operand_count(inst);
  IRValueNode *op1, *op2, *op3;

  switch (inst->opcode)
  {
  case IR_OP_RET: {
    IRType *func_ret_type = func->return_type;
    if (func_ret_type->kind == IR_TYPE_VOID)
    {
      VERIFY_ASSERT(op_count == 0, vctx, value, "'ret' in a void function must have 0 operands.");
    }
    else
    {
      VERIFY_ASSERT(op_count == 1, vctx, value, "'ret' in a non-void function must have 1 operand.");
      op1 = get_operand(inst, 0);
      VERIFY_ASSERT(op1->type == func_ret_type, vctx, value, "'ret' operand type mismatch function return type.");
    }
    break;
  }
  case IR_OP_BR: {
    VERIFY_ASSERT(op_count == 1, vctx, value, "'br' instruction must have exactly 1 operand (target_bb).");
    op1 = get_operand(inst, 0);
    VERIFY_ASSERT(op1->kind == IR_KIND_BASIC_BLOCK, vctx, op1, "'br' operand must be a Basic Block.");
    break;
  }
  case IR_OP_COND_BR: {
    VERIFY_ASSERT(op_count == 3, vctx, value, "'cond_br' must have exactly 3 operands (cond, true_bb, false_bb).");
    op1 = get_operand(inst, 0); /// cond
    op2 = get_operand(inst, 1); /// true_bb
    op3 = get_operand(inst, 2); /// false_bb
    VERIFY_ASSERT(op1->type->kind == IR_TYPE_I1, vctx, op1, "'cond_br' condition must be of type i1.");
    VERIFY_ASSERT(op2->kind == IR_KIND_BASIC_BLOCK, vctx, op2, "'cond_br' true target must be a Basic Block.");
    VERIFY_ASSERT(op3->kind == IR_KIND_BASIC_BLOCK, vctx, op3, "'cond_br' false target must be a Basic Block.");
    break;
  }
  case IR_OP_SWITCH: {
    VERIFY_ASSERT(op_count >= 2, vctx, value, "'switch' must have at least 2 operands (cond, default).");
    VERIFY_ASSERT(op_count % 2 == 0, vctx, value, "'switch' must have an even number of operands ([val, bb] pairs).");
    op1 = get_operand(inst, 0); /// cond
    op2 = get_operand(inst, 1); /// default_bb
    VERIFY_ASSERT(ir_type_is_integer(op1->type), vctx, op1, "'switch' condition must be an integer type.");
    VERIFY_ASSERT(op2->kind == IR_KIND_BASIC_BLOCK, vctx, op2, "'switch' default target must be a Basic Block.");
    for (int i = 2; i < op_count; i += 2)
    {
      IRValueNode *case_val = get_operand(inst, i);
      IRValueNode *case_bb = get_operand(inst, i + 1);
      VERIFY_ASSERT(case_val->kind == IR_KIND_CONSTANT, vctx, case_val, "'switch' case value must be a constant.");
      VERIFY_ASSERT(case_val->type == op1->type, vctx, case_val, "'switch' case value type must match condition type.");
      VERIFY_ASSERT(case_bb->kind == IR_KIND_BASIC_BLOCK, vctx, case_bb, "'switch' case target must be a Basic Block.");
    }
    break;
  }
  default: /// 应永不被击中
    VERIFY_ERROR(vctx, value, "Internal verifier error: Non-terminator in verify_op_terminator.");
  }
  return true;
}

/**
 * @brief [!!] (Refactored) 验证整数和位二元运算
 */
static bool
verify_op_int_binary(VerifierContext *vctx, IRInstruction *inst)
{
  IRValueNode *value = &inst->result;
  IRType *result_type = value->type;
  int op_count = get_operand_count(inst);

  VERIFY_ASSERT(op_count == 2, vctx, value, "Integer/Bitwise binary op must have 2 operands.");
  IRValueNode *op1 = get_operand(inst, 0);
  IRValueNode *op2 = get_operand(inst, 1);
  VERIFY_ASSERT(op1->type == op2->type, vctx, value, "Binary op operands must have the same type.");
  VERIFY_ASSERT(ir_type_is_integer(op1->type), vctx, op1, "Binary op operands must be integer type.");
  VERIFY_ASSERT(result_type == op1->type, vctx, value, "Binary op result type must match operand type.");
  return true;
}

/**
 * @brief [!!] (Refactored) 验证浮点二元运算
 */
static bool
verify_op_float_binary(VerifierContext *vctx, IRInstruction *inst)
{
  IRValueNode *value = &inst->result;
  IRType *result_type = value->type;
  int op_count = get_operand_count(inst);

  VERIFY_ASSERT(op_count == 2, vctx, value, "Floating point binary op must have 2 operands.");
  IRValueNode *op1 = get_operand(inst, 0);
  IRValueNode *op2 = get_operand(inst, 1);
  VERIFY_ASSERT(op1->type == op2->type, vctx, value, "Binary op operands must have the same type.");
  VERIFY_ASSERT(ir_type_is_floating(op1->type), vctx, op1, "Binary op operands must be floating point type.");
  VERIFY_ASSERT(result_type == op1->type, vctx, value, "Binary op result type must match operand type.");
  return true;
}

/**
 * @brief [!!] (Refactored) 验证比较指令
 */
static bool
verify_op_compare(VerifierContext *vctx, IRInstruction *inst)
{
  IRValueNode *value = &inst->result;
  IRType *result_type = value->type;
  int op_count = get_operand_count(inst);

  VERIFY_ASSERT(op_count == 2, vctx, value, "Compare op must have 2 operands.");
  IRValueNode *op1 = get_operand(inst, 0);
  IRValueNode *op2 = get_operand(inst, 1);
  VERIFY_ASSERT(op1->type == op2->type, vctx, value, "Compare op operands must have the same type.");
  VERIFY_ASSERT(result_type->kind == IR_TYPE_I1, vctx, value, "Compare op result type must be i1.");

  if (inst->opcode == IR_OP_ICMP)
  {
    bool is_valid_type = ir_type_is_integer(op1->type) || ir_type_is_pointer(op1->type);
    VERIFY_ASSERT(is_valid_type, vctx, op1, "'icmp' operands must be integer or pointer type.");
  }
  else // IR_OP_FCMP
  {
    VERIFY_ASSERT(ir_type_is_floating(op1->type), vctx, op1, "'fcmp' operands must be floating point type.");
  }
  return true;
}

/**
 * @brief [!!] (Refactored) 验证内存指令
 */
static bool
verify_op_memory(VerifierContext *vctx, IRInstruction *inst)
{
  IRValueNode *value = &inst->result;
  IRType *result_type = value->type;
  IRFunction *func = vctx->current_function;
  IRContext *ctx = func->parent->context;
  int op_count = get_operand_count(inst);
  IRValueNode *op1, *op2;

  switch (inst->opcode)
  {
  case IR_OP_ALLOCA: {
    VERIFY_ASSERT(op_count == 0, vctx, value, "'alloca' instruction should have no operands.");
    VERIFY_ASSERT(result_type->kind == IR_TYPE_PTR, vctx, value, "'alloca' result must be a pointer type.");
    IDList *entry_bb_node = func->basic_blocks.next;
    IRBasicBlock *entry_block = list_entry(entry_bb_node, IRBasicBlock, list_node);
    VERIFY_ASSERT(inst->parent == entry_block, vctx, value,
                  "'alloca' instruction must be in the function's entry block.");
    break;
  }
  case IR_OP_LOAD: {
    VERIFY_ASSERT(op_count == 1, vctx, value, "'load' must have exactly one operand (the pointer).");
    op1 = get_operand(inst, 0);
    VERIFY_ASSERT(op1->type->kind == IR_TYPE_PTR, vctx, op1, "'load' operand must be a pointer type.");
    IRType *pointee_type = op1->type->as.pointee_type;
    VERIFY_ASSERT(result_type == pointee_type, vctx, value,
                  "'load' result type must match the pointer's pointee type.");
    break;
  }
  case IR_OP_STORE: {
    VERIFY_ASSERT(op_count == 2, vctx, value, "'store' must have exactly 2 operands (value, pointer).");
    op1 = get_operand(inst, 0); // val_to_store
    op2 = get_operand(inst, 1); // ptr
    VERIFY_ASSERT(op2->type->kind == IR_TYPE_PTR, vctx, op2, "'store' second operand must be a pointer type.");
    IRType *pointee_type = op2->type->as.pointee_type;
    VERIFY_ASSERT(op1->type == pointee_type, vctx, value, "'store' value type must match the pointer's pointee type.");
    VERIFY_ASSERT(result_type->kind == IR_TYPE_VOID, vctx, value, "'store' instruction result type must be void.");
    break;
  }
  case IR_OP_GEP: {
    IRType *current_type = inst->as.gep.source_type;
    for (int i = 1; i < op_count; i++)
    {
      IRValueNode *index_val = get_operand(inst, i);
      VERIFY_ASSERT(ir_type_is_integer(index_val->type), vctx, index_val, "GEP index must be an integer type.");
      if (i == 1)
      {
        continue;
      }
      switch (current_type->kind)
      {
      case IR_TYPE_ARRAY:
        current_type = current_type->as.array.element_type;
        break;
      case IR_TYPE_STRUCT: {
        VERIFY_ASSERT(index_val->kind == IR_KIND_CONSTANT, vctx, index_val,
                      "GEP index into a struct *must* be a constant integer.");
        IRConstant *k = (IRConstant *)index_val;
        VERIFY_ASSERT(k->const_kind == CONST_KIND_INT, vctx, index_val,
                      "GEP struct index is a constant, but not an *integer* constant.");
        uint64_t member_idx = (uint64_t)k->data.int_val;
        VERIFY_ASSERT(member_idx < current_type->as.aggregate.member_count, vctx, index_val,
                      "GEP struct index is out of bounds.");
        current_type = current_type->as.aggregate.member_types[member_idx];
        break;
      }
      default:
        VERIFY_ERROR(vctx, &inst->result, "GEP is trying to index into a non-aggregate type (e.g., i32, ptr).");
      }
    }
    IRType *expected_result_type = ir_type_get_ptr(ctx, current_type);
    VERIFY_ASSERT(result_type == expected_result_type, vctx, &inst->result, "GEP result type is incorrect.");
    break;
  }
  default: // 应永不被击中
    VERIFY_ERROR(vctx, value, "Internal verifier error: Non-memory op in verify_op_memory.");
  }
  return true;
}

/**
 * @brief [!!] (Refactored) 验证类型转换指令
 */
static bool
verify_op_cast(VerifierContext *vctx, IRInstruction *inst)
{
  IRValueNode *value = &inst->result;
  IRType *result_type = value->type;
  int op_count = get_operand_count(inst);

  VERIFY_ASSERT(op_count == 1, vctx, value, "Cast instruction must have exactly 1 operand.");
  IRValueNode *op1 = get_operand(inst, 0);
  IRType *src_type = op1->type;
  IRType *dest_type = result_type;

  switch (inst->opcode)
  {
  case IR_OP_TRUNC:
  case IR_OP_ZEXT:
  case IR_OP_SEXT:
    VERIFY_ASSERT(ir_type_is_integer(src_type) && ir_type_is_integer(dest_type), vctx, value,
                  "Cast must be integer to integer.");
    // [TODO] 以后可以添加类型大小(size)检查
    break;
  case IR_OP_FPTRUNC:
  case IR_OP_FPEXT:
    VERIFY_ASSERT(ir_type_is_floating(src_type) && ir_type_is_floating(dest_type), vctx, value,
                  "Cast must be float to float.");
    break;
  case IR_OP_FPTOUI:
  case IR_OP_FPTOSI:
    VERIFY_ASSERT(ir_type_is_floating(src_type) && ir_type_is_integer(dest_type), vctx, value,
                  "Cast must be float to integer.");
    break;
  case IR_OP_UITOFP:
  case IR_OP_SITOFP:
    VERIFY_ASSERT(ir_type_is_integer(src_type) && ir_type_is_floating(dest_type), vctx, value,
                  "Cast must be integer to float.");
    break;
  case IR_OP_PTRTOINT:
    VERIFY_ASSERT(ir_type_is_pointer(src_type) && ir_type_is_integer(dest_type), vctx, value,
                  "Cast must be pointer to integer.");
    break;
  case IR_OP_INTTOPTR:
    VERIFY_ASSERT(ir_type_is_integer(src_type) && ir_type_is_pointer(dest_type), vctx, value,
                  "Cast must be integer to pointer.");
    break;
  case IR_OP_BITCAST:
    VERIFY_ASSERT(src_type->kind < IR_TYPE_ARRAY && dest_type->kind < IR_TYPE_ARRAY, vctx, value,
                  "bitcast does not support aggregate types (array/struct).");
    break;
  default: // 应永不被击中
    VERIFY_ERROR(vctx, value, "Internal verifier error: Non-cast op in verify_op_cast.");
  }
  return true;
}

/**
 * @brief [!!] (Refactored) 验证 PHI 指令
 */
static bool
verify_op_phi(VerifierContext *vctx, IRInstruction *inst)
{
  IRValueNode *value = &inst->result;
  IRType *result_type = value->type;
  IRFunction *func = vctx->current_function;
  IRBasicBlock *bb = vctx->current_block;
  int op_count = get_operand_count(inst);

  VERIFY_ASSERT(op_count > 0, vctx, value, "'phi' node cannot be empty.");
  VERIFY_ASSERT(op_count % 2 == 0, vctx, value, "'phi' node must have an even number of operands ([val, bb] pairs).");

  int phi_entry_count = op_count / 2;
  for (int i = 0; i < op_count; i += 2)
  {
    IRValueNode *val = get_operand(inst, i);
    IRValueNode *incoming_bb_val = get_operand(inst, i + 1);
    VERIFY_ASSERT(val->type == result_type, vctx, val, "PHI incoming value type mismatch.");
    VERIFY_ASSERT(incoming_bb_val->kind == IR_KIND_BASIC_BLOCK, vctx, incoming_bb_val,
                  "PHI incoming block must be a Basic Block.");
    for (int j = i + 2; j < op_count; j += 2)
    {
      IRValueNode *other_bb_val = get_operand(inst, j);
      VERIFY_ASSERT(incoming_bb_val != other_bb_val, vctx, &inst->result,
                    "PHI node contains duplicate entry for the same incoming block.");
    }
  }

  int actual_pred_count = 0;
  IDList *all_blocks_iter;
  list_for_each(&func->basic_blocks, all_blocks_iter)
  {
    IRBasicBlock *potential_pred = list_entry(all_blocks_iter, IRBasicBlock, list_node);
    if (potential_pred == bb)
      continue;
    if (list_empty(&potential_pred->instructions))
      continue;
    IRInstruction *term = list_entry(potential_pred->instructions.prev, IRInstruction, list_node);
    if (is_terminator_predecessor(term, bb))
    {
      actual_pred_count++;
      bool found_in_phi = find_in_phi(inst, potential_pred);
      VERIFY_ASSERT(found_in_phi, vctx, &inst->result, "PHI node is missing an entry for predecessor block '%s'.",
                    potential_pred->label_address.name);
    }
  }

  VERIFY_ASSERT(phi_entry_count == actual_pred_count, vctx, &inst->result,
                "PHI node has incorrect number of entries. Found %d, but expected %d (actual predecessors).",
                phi_entry_count, actual_pred_count);
  return true;
}

/**
 * @brief [!!] (Refactored) 验证 CALL 指令
 */
static bool
verify_op_call(VerifierContext *vctx, IRInstruction *inst)
{
  IRValueNode *value = &inst->result;
  IRType *result_type = value->type;
  int op_count = get_operand_count(inst);

  VERIFY_ASSERT(op_count >= 1, vctx, value, "'call' must have at least 1 operand (the callee).");
  IRValueNode *op1 = get_operand(inst, 0); // callee_val
  VERIFY_ASSERT(op1->type->kind == IR_TYPE_PTR, vctx, op1, "'call' callee must be a pointer type.");

  IRType *callee_pointee_type = op1->type->as.pointee_type;
  VERIFY_ASSERT(callee_pointee_type->kind == IR_TYPE_FUNCTION, vctx, op1,
                "'call' callee must be a *pointer to a function type*.");

  IRType *func_type = callee_pointee_type;
  VERIFY_ASSERT(result_type == func_type->as.function.return_type, vctx, value,
                "'call' result type does not match callee's function type return type.");

  size_t expected_arg_count = func_type->as.function.param_count;
  size_t provided_arg_count = op_count - 1;
  bool is_variadic = func_type->as.function.is_variadic;

  if (is_variadic)
  {
    VERIFY_ASSERT(provided_arg_count >= expected_arg_count, vctx, value,
                  "'call' to variadic function expected at least %zu args, but got %zu.", expected_arg_count,
                  provided_arg_count);
  }
  else
  {
    VERIFY_ASSERT(provided_arg_count == expected_arg_count, vctx, value,
                  "'call' argument count mismatch. Expected %zu, but got %zu.", expected_arg_count, provided_arg_count);
  }

  for (size_t i = 0; i < expected_arg_count; i++)
  {
    IRValueNode *provided_arg = get_operand(inst, i + 1);
    IRType *expected_type = func_type->as.function.param_types[i];
    VERIFY_ASSERT(provided_arg->type == expected_type, vctx, provided_arg,
                  "'call' argument %zu type mismatch. Expected type %p, got %p.", i, expected_type, provided_arg->type);
  }
  return true;
}

/**
 * @brief 验证单条指令
 */
static bool
verify_instruction(VerifierContext *vctx, IRInstruction *inst)
{
  /// --- 1. 上下文和 Use-Def 链基础检查 ---
  IRValueNode *value = &inst->result;
  IRBasicBlock *bb = inst->parent;
  VERIFY_ASSERT(bb != NULL, vctx, value, "Instruction has no parent BasicBlock.");

  IRFunction *func = bb->parent;
  VERIFY_ASSERT(func != NULL, vctx, value, "Instruction's parent block has no parent Function.");

  IRContext *ctx = func->parent->context;
  VERIFY_ASSERT(ctx != NULL, vctx, value, "Failed to get Context from Function.");

  IRType *result_type = value->type;
  VERIFY_ASSERT(result_type != NULL, vctx, value, "Instruction result has NULL type.");

  /// --- 2. SSA 支配规则检查 ---
  IDList *iter_node;
  list_for_each(&inst->operands, iter_node)
  {
    IRUse *use = list_entry(iter_node, IRUse, user_node);
    VERIFY_ASSERT(use->user == inst, vctx, value, "Inconsistent Use-Def chain: use->user points to wrong instruction.");
    VERIFY_ASSERT(use->value != NULL, vctx, value, "Instruction has a NULL operand (use->value is NULL).");
    VERIFY_ASSERT(use->value->type != NULL, vctx, use->value, "Instruction operand has NULL type.");

    if (use->value->kind != IR_KIND_INSTRUCTION)
    {
      continue; /// 常量、参数、BB 等不需要支配检查
    }
    if (inst->opcode == IR_OP_PHI)
    {
      continue; /// PHI 节点有特殊的 SSA 规则 (由 PHI 验证器自己处理)
    }

    IRInstruction *def_inst = container_of(use->value, IRInstruction, result);
    IRBasicBlock *def_bb = def_inst->parent;
    IRBasicBlock *use_bb = inst->parent;

    if (def_bb == use_bb)
    {
      bool def_found_before_use = false;
      IDList *prev_node = inst->list_node.prev;
      while (prev_node != &use_bb->instructions)
      {
        if (prev_node == &def_inst->list_node)
        {
          def_found_before_use = true;
          break;
        }
        prev_node = prev_node->prev;
      }
      VERIFY_ASSERT(def_found_before_use, vctx, &inst->result,
                    "SSA Violation: Instruction operand is used *before* it is defined in the same basic block.");
    }
    else
    {
      bool dominates = dom_tree_dominates(vctx->dom_tree, def_bb, use_bb);
      VERIFY_ASSERT(dominates, vctx, &inst->result,
                    "SSA VIOLATION: Definition in block '%s' does not dominate use in block '%s'.",
                    def_bb->label_address.name, use_bb->label_address.name);
    }
  }

  /// --- 3. 特定指令 (Opcode) 的规则检查 (分发) ---
  switch (inst->opcode)
  {
  // --- 终止指令 ---
  case IR_OP_RET:
  case IR_OP_BR:
  case IR_OP_COND_BR:
  case IR_OP_SWITCH:
    return verify_op_terminator(vctx, inst);

  // --- 整数/位运算 ---
  case IR_OP_ADD:
  case IR_OP_SUB:
  case IR_OP_MUL:
  case IR_OP_UDIV:
  case IR_OP_SDIV:
  case IR_OP_UREM:
  case IR_OP_SREM:
  case IR_OP_SHL:
  case IR_OP_LSHR:
  case IR_OP_ASHR:
  case IR_OP_AND:
  case IR_OP_OR:
  case IR_OP_XOR:
    return verify_op_int_binary(vctx, inst);

  // --- 浮点运算 ---
  case IR_OP_FADD:
  case IR_OP_FSUB:
  case IR_OP_FMUL:
  case IR_OP_FDIV:
    return verify_op_float_binary(vctx, inst);

  // --- 比较 ---
  case IR_OP_ICMP:
  case IR_OP_FCMP:
    return verify_op_compare(vctx, inst);

  // --- 内存 ---
  case IR_OP_ALLOCA:
  case IR_OP_LOAD:
  case IR_OP_STORE:
  case IR_OP_GEP:
    return verify_op_memory(vctx, inst);

  // --- 类型转换 ---
  case IR_OP_TRUNC:
  case IR_OP_ZEXT:
  case IR_OP_SEXT:
  case IR_OP_FPTRUNC:
  case IR_OP_FPEXT:
  case IR_OP_FPTOUI:
  case IR_OP_FPTOSI:
  case IR_OP_UITOFP:
  case IR_OP_SITOFP:
  case IR_OP_PTRTOINT:
  case IR_OP_INTTOPTR:
  case IR_OP_BITCAST:
    return verify_op_cast(vctx, inst);

  /// --- 其他 ---
  case IR_OP_PHI:
    return verify_op_phi(vctx, inst);
  case IR_OP_CALL:
    return verify_op_call(vctx, inst);

  default:
    VERIFY_ERROR(vctx, value, "Unknown or unimplemented instruction opcode in verifier: %d", inst->opcode);
  }
}

/**
 * @brief 验证单个基本块
 */
static bool
verify_basic_block(VerifierContext *vctx, IRBasicBlock *bb)
{
  VERIFY_ASSERT(bb != NULL, vctx, NULL, "BasicBlock is NULL.");
  VERIFY_ASSERT(bb->parent != NULL, vctx, &bb->label_address, "BasicBlock has no parent Function.");

  vctx->current_block = bb;

  if (list_empty(&bb->instructions))
  {
    VERIFY_ERROR(vctx, &bb->label_address, "BasicBlock cannot be empty. Must have at least one terminator.");
  }

  IDList *last_inst_node = bb->instructions.prev;
  IRInstruction *last_inst = list_entry(last_inst_node, IRInstruction, list_node);

  // [!!] 使用新的辅助函数
  VERIFY_ASSERT(ir_is_terminator(last_inst->opcode), vctx, &last_inst->result,
                "BasicBlock must end with a terminator instruction (ret, br, cond_br, switch).");

  bool processing_phis = true;
  IDList *iter_node;
  list_for_each(&bb->instructions, iter_node)
  {
    IRInstruction *inst = list_entry(iter_node, IRInstruction, list_node);

    VERIFY_ASSERT(inst->parent == bb, vctx, &inst->result, "Instruction's parent pointer is incorrect.");

    if (inst->opcode == IR_OP_PHI)
    {
      VERIFY_ASSERT(processing_phis, vctx, &inst->result, "PHI instruction found after non-PHI instruction.");
    }
    else
    {
      processing_phis = false;
    }

    if (inst != last_inst)
    {
      // [!!] 使用新的辅助函数
      VERIFY_ASSERT(!ir_is_terminator(inst->opcode), vctx, &inst->result,
                    "Terminator instruction found in the middle of a BasicBlock.");
    }

    if (!verify_instruction(vctx, inst))
    {
      return false;
    }
  }

  vctx->current_block = NULL;
  return true;
}

/*
 * =================================================================
 * --- 公共 API 实现 ---
 * =================================================================
 */

bool
ir_verify_function(IRFunction *func)
{
  IRPrinter p;
  ir_printer_init_file(&p, stderr);
  VerifierContext vctx = {0};
  vctx.p = &p;

  VERIFY_ASSERT(func != NULL, &vctx, NULL, "Function is NULL.");
  VERIFY_ASSERT(func->parent != NULL, &vctx, &func->entry_address, "Function has no parent Module.");
  VERIFY_ASSERT(func->return_type != NULL, &vctx, &func->entry_address, "Function has NULL return type.");

  vctx.current_function = func;

  bump_init(&vctx.analysis_arena);
  FunctionCFG *cfg = NULL;
  DominatorTree *doms = NULL;

  if (list_empty(&func->basic_blocks))
  {

    IDList *arg_it;
    list_for_each(&func->arguments, arg_it)
    {
      IRArgument *arg = list_entry(arg_it, IRArgument, list_node);

      VERIFY_ASSERT(arg->value.type != NULL, &vctx, &arg->value, "Argument has NULL type.");
      VERIFY_ASSERT(arg->value.type->kind != IR_TYPE_VOID, &vctx, &arg->value,
                    "Function argument cannot have void type.");
    }
    bump_destroy(&vctx.analysis_arena);
    return !vctx.has_error;
  }

  cfg = cfg_build(func, &vctx.analysis_arena);

  doms = dom_tree_build(cfg, &vctx.analysis_arena);

  vctx.dom_tree = doms;

  IDList *arg_it;
  list_for_each(&func->arguments, arg_it)
  {
    IRArgument *arg = list_entry(arg_it, IRArgument, list_node);
    VERIFY_ASSERT(arg->parent == func, &vctx, &arg->value, "Argument's parent pointer is incorrect.");
    VERIFY_ASSERT(arg->value.type != NULL, &vctx, &arg->value, "Argument has NULL type.");
    VERIFY_ASSERT(arg->value.type->kind != IR_TYPE_VOID, &vctx, &arg->value,
                  "Function argument cannot have void type.");

    VERIFY_ASSERT(arg->value.name != NULL, &vctx, &arg->value, "Argument in a function *definition* must have a name.");
  }

  IDList *bb_it;
  list_for_each(&func->basic_blocks, bb_it)
  {
    IRBasicBlock *bb = list_entry(bb_it, IRBasicBlock, list_node);
    VERIFY_ASSERT(bb->parent == func, &vctx, &bb->label_address, "BasicBlock's parent pointer is incorrect.");

    if (!verify_basic_block(&vctx, bb))
    {

      if (doms)
        dom_tree_destroy(doms);
      if (cfg)
        cfg_destroy(cfg);
      bump_destroy(&vctx.analysis_arena);
      return false;
    }
  }

  if (doms)
    dom_tree_destroy(doms);
  if (cfg)
    cfg_destroy(cfg);
  bump_destroy(&vctx.analysis_arena);

  return !vctx.has_error;
}

bool
ir_verify_module(IRModule *mod)
{

  IRPrinter p;
  ir_printer_init_file(&p, stderr);

  VerifierContext vctx = {0};
  vctx.p = &p;

  VERIFY_ASSERT(mod != NULL, &vctx, NULL, "Module is NULL.");
  VERIFY_ASSERT(mod->context != NULL, &vctx, NULL, "Module has no Context.");

  IDList *global_it;
  list_for_each(&mod->globals, global_it)
  {
    IRGlobalVariable *global = list_entry(global_it, IRGlobalVariable, list_node);

    VERIFY_ASSERT(global->parent == mod, &vctx, &global->value, "Global's parent pointer is incorrect.");
    VERIFY_ASSERT(global->allocated_type != NULL, &vctx, &global->value, "Global has NULL allocated_type.");
    VERIFY_ASSERT(global->value.type->kind == IR_TYPE_PTR, &vctx, &global->value,
                  "Global's value must be a pointer type.");

    if (global->initializer)
    {

      bool is_valid_initializer = (global->initializer->kind == IR_KIND_CONSTANT) ||
                                  (global->initializer->kind == IR_KIND_FUNCTION) ||
                                  (global->initializer->kind == IR_KIND_GLOBAL);
      VERIFY_ASSERT(is_valid_initializer, &vctx, global->initializer,
                    "Global initializer must be a constant, function, or another global.");
      VERIFY_ASSERT(global->initializer->type == global->allocated_type, &vctx, global->initializer,
                    "Global initializer type mismatch allocated_type.");
    }
  }

  IDList *func_it;
  list_for_each(&mod->functions, func_it)
  {
    IRFunction *func = list_entry(func_it, IRFunction, list_node);
    VERIFY_ASSERT(func->parent == mod, &vctx, &func->entry_address, "Function's parent pointer is incorrect.");

    if (!ir_verify_function(func))
    {

      return false;
    }
  }

  return !vctx.has_error;
}
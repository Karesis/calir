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

#include "interpreter/interpreter.h"

#include "ir/basicblock.h"
#include "ir/constant.h"
#include "ir/function.h"
#include "ir/global.h"
#include "ir/instruction.h"
#include "ir/type.h"
#include "ir/use.h"
#include "ir/value.h"

#include "utils/bump.h"
#include "utils/data_layout.h"
#include "utils/hashmap.h"
#include "utils/id_list.h"

#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define INTERP_STACK_SIZE (1024 * 1024)

/*
 * =================================================================
 * --- 辅助函数 (Helpers) ---
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

static IRValueNode *
get_operand_node(IRInstruction *inst, int index)
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
static RuntimeValueKind
ir_to_runtime_kind(IRTypeKind kind)
{
  switch (kind)
  {
  case IR_TYPE_I1:
    return RUNTIME_VAL_I1;
  case IR_TYPE_I8:
    return RUNTIME_VAL_I8;
  case IR_TYPE_I16:
    return RUNTIME_VAL_I16;
  case IR_TYPE_I32:
    return RUNTIME_VAL_I32;
  case IR_TYPE_I64:
    return RUNTIME_VAL_I64;
  case IR_TYPE_F32:
    return RUNTIME_VAL_F32;
  case IR_TYPE_F64:
    return RUNTIME_VAL_F64;
  case IR_TYPE_PTR:
    return RUNTIME_VAL_PTR;
  default:
    return RUNTIME_VAL_UNDEF;
  }
}

static RuntimeValue *
eval_constant(ExecutionContext *ctx, IRConstant *constant)
{
  IRValueNode *val_node = &constant->value;
  RuntimeValue *rt_val = BUMP_ALLOC_ZEROED(ctx->interp->arena, RuntimeValue);
  switch (constant->const_kind)
  {
  case CONST_KIND_UNDEF:
    rt_val->kind = RUNTIME_VAL_UNDEF;
    break;
  case CONST_KIND_INT:
    rt_val->kind = ir_to_runtime_kind(val_node->type->kind);
    switch (rt_val->kind)
    {
    case RUNTIME_VAL_I1:
      rt_val->as.val_i1 = (bool)constant->data.int_val;
      break;
    case RUNTIME_VAL_I8:
      rt_val->as.val_i8 = (int8_t)constant->data.int_val;
      break;
    case RUNTIME_VAL_I16:
      rt_val->as.val_i16 = (int16_t)constant->data.int_val;
      break;
    case RUNTIME_VAL_I32:
      rt_val->as.val_i32 = (int32_t)constant->data.int_val;
      break;
    case RUNTIME_VAL_I64:
      rt_val->as.val_i64 = (int64_t)constant->data.int_val;
      break;
    default:
      assert(false && "Invalid integer constant type");
    }
    break;
  case CONST_KIND_FLOAT:
    rt_val->kind = ir_to_runtime_kind(val_node->type->kind);
    switch (rt_val->kind)
    {
    case RUNTIME_VAL_F32:
      rt_val->as.val_f32 = (float)constant->data.float_val;
      break;
    case RUNTIME_VAL_F64:
      rt_val->as.val_f64 = (double)constant->data.float_val;
      break;
    default:
      assert(false && "Invalid float constant type");
    }
    break;
  }
  ptr_hashmap_put(ctx->frame, val_node, rt_val);
  return rt_val;
}

static RuntimeValue *
get_value(ExecutionContext *ctx, IRValueNode *val_node)
{
  RuntimeValue *rt_val = ptr_hashmap_get(ctx->frame, val_node);
  if (rt_val)
  {
    return rt_val;
  }
  if (val_node->kind == IR_KIND_CONSTANT)
  {
    IRConstant *constant = container_of(val_node, IRConstant, value);
    return eval_constant(ctx, constant);
  }
  assert(false && "Interpreter Error: Use of undefined value");
  return NULL;
}
static void
set_value(ExecutionContext *ctx, IRValueNode *val_node, RuntimeValue *rt_val)
{
  ptr_hashmap_put(ctx->frame, val_node, rt_val);
}

/*
 * =================================================================
 * --- [!!] (新增) 指令执行辅助函数 [!!] ---
 * =================================================================
 */

/**
 * @brief 执行 'select' 指令
 */
static ExecutionResultKind
execute_op_select(ExecutionContext *ctx, IRInstruction *inst)
{
  RuntimeValue *rt_cond = get_value(ctx, get_operand_node(inst, 0));
  RuntimeValue *rt_true_val = get_value(ctx, get_operand_node(inst, 1));
  RuntimeValue *rt_false_val = get_value(ctx, get_operand_node(inst, 2));

  assert(rt_cond->kind == RUNTIME_VAL_I1 && "Select condition must be i1");

  RuntimeValue *rt_res = (rt_cond->as.val_i1) ? rt_true_val : rt_false_val;

  set_value(ctx, &inst->result, rt_res);
  return EXEC_OK;
}

/**
 * @brief 执行整数/位运算
 */
static ExecutionResultKind
execute_op_int_binary(ExecutionContext *ctx, IRInstruction *inst)
{
  RuntimeValue *rt_lhs = get_value(ctx, get_operand_node(inst, 0));
  RuntimeValue *rt_rhs = get_value(ctx, get_operand_node(inst, 1));
  RuntimeValue *rt_res = BUMP_ALLOC_ZEROED(&ctx->value_arena, RuntimeValue);

  rt_res->kind = rt_lhs->kind;

  // [!!] (已重构) 按类型分别处理，以确保正确的溢出/环绕
  switch (rt_lhs->kind)
  {
  case RUNTIME_VAL_I1: {
    // 将 i1 视为 i8 进行计算
    int8_t lhs = (int8_t)rt_lhs->as.val_i1;
    int8_t rhs = (int8_t)rt_rhs->as.val_i1;
    int8_t res = 0;
    const uint8_t bit_width = 1;
    // (i1 移位是特殊的)
    uint8_t safe_amt = (uint8_t)rhs & (bit_width - 1); // 掩码到 0

    switch (inst->opcode)
    {
    case IR_OP_ADD:
      res = lhs + rhs;
      break;
    case IR_OP_SUB:
      res = lhs - rhs;
      break;
    case IR_OP_MUL:
      res = lhs * rhs;
      break;
    case IR_OP_SDIV:
    case IR_OP_SREM:
    case IR_OP_UDIV:
    case IR_OP_UREM:
      ctx->error_message = "Runtime Error: Division/Remainder on i1 type is invalid.";
      return EXEC_ERR_INVALID_PTR;
    // 位运算
    case IR_OP_SHL:
      res = (int8_t)((uint8_t)lhs << safe_amt);
      break; // 总是 0
    case IR_OP_LSHR:
      res = (int8_t)((uint8_t)lhs >> safe_amt);
      break; // 总是 0
    case IR_OP_ASHR:
      res = lhs >> safe_amt;
      break; // 总是 0
    case IR_OP_AND:
      res = lhs & rhs;
      break;
    case IR_OP_OR:
      res = lhs | rhs;
      break;
    case IR_OP_XOR:
      res = lhs ^ rhs;
      break;
    default:
      assert(false && "unreachable");
    }
    rt_res->as.val_i1 = (bool)(res & 1); // 写回 i1
    break;
  }

  case RUNTIME_VAL_I8: {
    int8_t lhs = rt_lhs->as.val_i8;
    int8_t rhs = rt_rhs->as.val_i8;
    int8_t res = 0;

    // [!!] (修复 UB) 清理移位量
    const uint8_t bit_width = 8;
    // 1. 将 rhs (int8_t) 视为无符号 (uint8_t)
    // 2. 掩码到 [0, 7]
    uint8_t safe_amt = (uint8_t)rhs & (bit_width - 1);

    switch (inst->opcode)
    {
    case IR_OP_ADD:
      res = lhs + rhs;
      break;
    case IR_OP_SUB:
      res = lhs - rhs;
      break;
    case IR_OP_MUL:
      res = lhs * rhs;
      break;
    case IR_OP_SDIV:
      if (rhs == 0)
      {
        ctx->error_message = "Runtime Error: i8 Signed division by zero";
        return EXEC_ERR_DIV_BY_ZERO_S;
      }
      if (lhs == INT8_MIN && rhs == -1)
      {
        res = INT8_MIN;
      }
      else
      {
        res = lhs / rhs;
      }
      break;
    case IR_OP_SREM:
      if (rhs == 0)
      {
        ctx->error_message = "Runtime Error: i8 Signed remainder by zero";
        return EXEC_ERR_DIV_BY_ZERO_S;
      }
      if (lhs == INT8_MIN && rhs == -1)
      {
        res = 0;
      }
      else
      {
        res = lhs % rhs;
      }
      break;
    case IR_OP_UDIV:
      if (rhs == 0)
      {
        ctx->error_message = "Runtime Error: i8 Unsigned division by zero";
        return EXEC_ERR_DIV_BY_ZERO_U;
      }
      res = (int8_t)((uint8_t)lhs / (uint8_t)rhs);
      break;
    case IR_OP_UREM:
      if (rhs == 0)
      {
        ctx->error_message = "Runtime Error: i8 Unsigned remainder by zero";
        return EXEC_ERR_DIV_BY_ZERO_U;
      }
      res = (int8_t)((uint8_t)lhs % (uint8_t)rhs);
      break;
    // [!!] (修复 UB)
    case IR_OP_SHL:
      res = (int8_t)((uint8_t)lhs << safe_amt);
      break;
    case IR_OP_LSHR:
      res = (int8_t)((uint8_t)lhs >> safe_amt);
      break;
    case IR_OP_ASHR:
      res = lhs >> safe_amt;
      break; // safe_amt 保证了 C 的 `>>` 是安全的

    case IR_OP_AND:
      res = lhs & rhs;
      break;
    case IR_OP_OR:
      res = lhs | rhs;
      break;
    case IR_OP_XOR:
      res = lhs ^ rhs;
      break;
    default:
      assert(false && "unreachable");
    }
    rt_res->as.val_i8 = res;
    break;
  }

  case RUNTIME_VAL_I16: {
    int16_t lhs = rt_lhs->as.val_i16;
    int16_t rhs = rt_rhs->as.val_i16;
    int16_t res = 0;

    // [!!] (修复 UB) 清理移位量
    const uint16_t bit_width = 16;
    uint16_t safe_amt = (uint16_t)rhs & (bit_width - 1);

    switch (inst->opcode)
    {
    case IR_OP_ADD:
      res = lhs + rhs;
      break;
    case IR_OP_SUB:
      res = lhs - rhs;
      break;
    case IR_OP_MUL:
      res = lhs * rhs;
      break;
    case IR_OP_SDIV:
      if (rhs == 0)
      {
        ctx->error_message = "Runtime Error: i16 Signed division by zero";
        return EXEC_ERR_DIV_BY_ZERO_S;
      }
      if (lhs == INT16_MIN && rhs == -1)
      {
        res = INT16_MIN;
      }
      else
      {
        res = lhs / rhs;
      }
      break;
    case IR_OP_SREM:
      if (rhs == 0)
      {
        ctx->error_message = "Runtime Error: i16 Signed remainder by zero";
        return EXEC_ERR_DIV_BY_ZERO_S;
      }
      if (lhs == INT16_MIN && rhs == -1)
      {
        res = 0;
      }
      else
      {
        res = lhs % rhs;
      }
      break;
    case IR_OP_UDIV:
      if (rhs == 0)
      {
        ctx->error_message = "Runtime Error: i16 Unsigned division by zero";
        return EXEC_ERR_DIV_BY_ZERO_U;
      }
      res = (int16_t)((uint16_t)lhs / (uint16_t)rhs);
      break;
    case IR_OP_UREM:
      if (rhs == 0)
      {
        ctx->error_message = "Runtime Error: i16 Unsigned remainder by zero";
        return EXEC_ERR_DIV_BY_ZERO_U;
      }
      res = (int16_t)((uint16_t)lhs % (uint16_t)rhs);
      break;
    // [!!] (修复 UB)
    case IR_OP_SHL:
      res = (int16_t)((uint16_t)lhs << safe_amt);
      break;
    case IR_OP_LSHR:
      res = (int16_t)((uint16_t)lhs >> safe_amt);
      break;
    case IR_OP_ASHR:
      res = lhs >> safe_amt;
      break;

    case IR_OP_AND:
      res = lhs & rhs;
      break;
    case IR_OP_OR:
      res = lhs | rhs;
      break;
    case IR_OP_XOR:
      res = lhs ^ rhs;
      break;
    default:
      assert(false && "unreachable");
    }
    rt_res->as.val_i16 = res;
    break;
  }

  case RUNTIME_VAL_I32: {
    int32_t lhs = rt_lhs->as.val_i32;
    int32_t rhs = rt_rhs->as.val_i32;
    int32_t res = 0;

    // [!!] (修复 UB) 清理移位量
    const uint32_t bit_width = 32;
    uint32_t safe_amt = (uint32_t)rhs & (bit_width - 1);

    switch (inst->opcode)
    {
    case IR_OP_ADD:
      res = lhs + rhs;
      break;
    case IR_OP_SUB:
      res = lhs - rhs;
      break;
    case IR_OP_MUL:
      res = lhs * rhs;
      break;
    case IR_OP_SDIV:
      if (rhs == 0)
      {
        ctx->error_message = "Runtime Error: i32 Signed division by zero";
        return EXEC_ERR_DIV_BY_ZERO_S;
      }
      if (lhs == INT32_MIN && rhs == -1)
      {
        res = INT32_MIN;
      }
      else
      {
        res = lhs / rhs;
      }
      break;
    case IR_OP_SREM:
      if (rhs == 0)
      {
        ctx->error_message = "Runtime Error: i32 Signed remainder by zero";
        return EXEC_ERR_DIV_BY_ZERO_S;
      }
      if (lhs == INT32_MIN && rhs == -1)
      {
        res = 0;
      }
      else
      {
        res = lhs % rhs;
      }
      break;
    case IR_OP_UDIV:
      if (rhs == 0)
      {
        ctx->error_message = "Runtime Error: i32 Unsigned division by zero";
        return EXEC_ERR_DIV_BY_ZERO_U;
      }
      res = (int32_t)((uint32_t)lhs / (uint32_t)rhs);
      break;
    case IR_OP_UREM:
      if (rhs == 0)
      {
        ctx->error_message = "Runtime Error: i32 Unsigned remainder by zero";
        return EXEC_ERR_DIV_BY_ZERO_U;
      }
      res = (int32_t)((uint32_t)lhs % (uint32_t)rhs);
      break;
    // [!!] (修复 UB)
    case IR_OP_SHL:
      res = (int32_t)((uint32_t)lhs << safe_amt);
      break;
    case IR_OP_LSHR:
      res = (int32_t)((uint32_t)lhs >> safe_amt);
      break;
    case IR_OP_ASHR:
      res = lhs >> safe_amt;
      break;

    case IR_OP_AND:
      res = lhs & rhs;
      break;
    case IR_OP_OR:
      res = lhs | rhs;
      break;
    case IR_OP_XOR:
      res = lhs ^ rhs;
      break;
    default:
      assert(false && "unreachable");
    }
    rt_res->as.val_i32 = res;
    break;
  }

  case RUNTIME_VAL_I64: {
    int64_t lhs = rt_lhs->as.val_i64;
    int64_t rhs = rt_rhs->as.val_i64;
    int64_t res = 0;

    // [!!] (修复 UB) 清理移位量
    const uint64_t bit_width = 64;
    uint64_t safe_amt = (uint64_t)rhs & (bit_width - 1);

    switch (inst->opcode)
    {
    case IR_OP_ADD:
      res = lhs + rhs;
      break;
    case IR_OP_SUB:
      res = lhs - rhs;
      break;
    case IR_OP_MUL:
      res = lhs * rhs;
      break;
    case IR_OP_SDIV:
      if (rhs == 0)
      {
        ctx->error_message = "Runtime Error: i64 Signed division by zero";
        return EXEC_ERR_DIV_BY_ZERO_S;
      }
      if (lhs == INT64_MIN && rhs == -1)
      {
        res = INT64_MIN;
      }
      else
      {
        res = lhs / rhs;
      }
      break;
    case IR_OP_SREM:
      if (rhs == 0)
      {
        ctx->error_message = "Runtime Error: i64 Signed remainder by zero";
        return EXEC_ERR_DIV_BY_ZERO_S;
      }
      if (lhs == INT64_MIN && rhs == -1)
      {
        res = 0;
      }
      else
      {
        res = lhs % rhs;
      }
      break;
    case IR_OP_UDIV:
      if (rhs == 0)
      {
        ctx->error_message = "Runtime Error: i64 Unsigned division by zero";
        return EXEC_ERR_DIV_BY_ZERO_U;
      }
      res = (int64_t)((uint64_t)lhs / (uint64_t)rhs);
      break;
    case IR_OP_UREM:
      if (rhs == 0)
      {
        ctx->error_message = "Runtime Error: i64 Unsigned remainder by zero";
        return EXEC_ERR_DIV_BY_ZERO_U;
      }
      res = (int64_t)((uint64_t)lhs % (uint64_t)rhs);
      break;
    // [!!] (修复 UB)
    case IR_OP_SHL:
      res = (int64_t)((uint64_t)lhs << safe_amt);
      break;
    case IR_OP_LSHR:
      res = (int64_t)((uint64_t)lhs >> safe_amt);
      break;
    case IR_OP_ASHR:
      res = lhs >> safe_amt;
      break;

    case IR_OP_AND:
      res = lhs & rhs;
      break;
    case IR_OP_OR:
      res = lhs | rhs;
      break;
    case IR_OP_XOR:
      res = lhs ^ rhs;
      break;
    default:
      assert(false && "unreachable");
    }
    rt_res->as.val_i64 = res;
    break;
  }

  default:
    assert(false && "Invalid type for integer/bitwise operation");
  }

  set_value(ctx, &inst->result, rt_res);
  return EXEC_OK;
}

/**
 * @brief [!!] (已实现) 执行浮点二元运算
 */
static ExecutionResultKind
execute_op_float_binary(ExecutionContext *ctx, IRInstruction *inst)
{
  RuntimeValue *rt_lhs = get_value(ctx, get_operand_node(inst, 0));
  RuntimeValue *rt_rhs = get_value(ctx, get_operand_node(inst, 1));
  RuntimeValue *rt_res = BUMP_ALLOC_ZEROED(&ctx->value_arena, RuntimeValue);

  rt_res->kind = rt_lhs->kind;

  switch (rt_lhs->kind)
  {
  case RUNTIME_VAL_F32:
  case RUNTIME_VAL_F64: {

    double lhs = (rt_lhs->kind == RUNTIME_VAL_F32) ? (double)rt_lhs->as.val_f32 : rt_lhs->as.val_f64;
    double rhs = (rt_lhs->kind == RUNTIME_VAL_F32) ? (double)rt_rhs->as.val_f32 : rt_rhs->as.val_f64;

    double res = 0.0;
    switch (inst->opcode)
    {
    case IR_OP_FADD:
      res = lhs + rhs;
      break;
    case IR_OP_FSUB:
      res = lhs - rhs;
      break;
    case IR_OP_FMUL:
      res = lhs * rhs;
      break;
    case IR_OP_FDIV:
      if (rhs == 0.0)
      {
        ctx->error_message = "Runtime Error: Float division by zero";
        return EXEC_ERR_DIV_BY_ZERO_F;
      }
      res = lhs / rhs;
      break;
    default:
      assert(false && "unreachable");
    }

    if (rt_res->kind == RUNTIME_VAL_F32)
    {
      rt_res->as.val_f32 = (float)res;
    }
    else
    {
      rt_res->as.val_f64 = res;
    }
    break;
  }
  default:
    assert(false && "Invalid type for float operation");
  }

  set_value(ctx, &inst->result, rt_res);
  return EXEC_OK;
}

/**
 * @brief [!!] (新增) 执行比较运算
 */
static ExecutionResultKind
execute_op_compare(ExecutionContext *ctx, IRInstruction *inst)
{
  RuntimeValue *rt_lhs = get_value(ctx, get_operand_node(inst, 0));
  RuntimeValue *rt_rhs = get_value(ctx, get_operand_node(inst, 1));
  RuntimeValue *rt_res = BUMP_ALLOC_ZEROED(&ctx->value_arena, RuntimeValue);

  rt_res->kind = RUNTIME_VAL_I1;

  switch (inst->opcode)
  {
  case IR_OP_ICMP: {

    int64_t lhs_s = 0;
    int64_t rhs_s = 0;
    uint64_t lhs_u = 0;
    uint64_t rhs_u = 0;
    switch (rt_lhs->kind)
    {
    case RUNTIME_VAL_I1:
      lhs_s = rt_lhs->as.val_i1;
      rhs_s = rt_rhs->as.val_i1;
      lhs_u = rt_lhs->as.val_i1;
      rhs_u = rt_rhs->as.val_i1;
      break;
    case RUNTIME_VAL_I8:
      lhs_s = rt_lhs->as.val_i8;
      rhs_s = rt_rhs->as.val_i8;
      lhs_u = (uint8_t)rt_lhs->as.val_i8;
      rhs_u = (uint8_t)rt_rhs->as.val_i8;
      break;
    case RUNTIME_VAL_I16:
      lhs_s = rt_lhs->as.val_i16;
      rhs_s = rt_rhs->as.val_i16;
      lhs_u = (uint16_t)rt_lhs->as.val_i16;
      rhs_u = (uint16_t)rt_rhs->as.val_i16;
      break;
    case RUNTIME_VAL_I32:
      lhs_s = rt_lhs->as.val_i32;
      rhs_s = rt_rhs->as.val_i32;
      lhs_u = (uint32_t)rt_lhs->as.val_i32;
      rhs_u = (uint32_t)rt_rhs->as.val_i32;
      break;
    case RUNTIME_VAL_I64:
      lhs_s = rt_lhs->as.val_i64;
      rhs_s = rt_rhs->as.val_i64;
      lhs_u = (uint64_t)rt_lhs->as.val_i64;
      rhs_u = (uint64_t)rt_rhs->as.val_i64;
      break;
    case RUNTIME_VAL_PTR:
      lhs_s = (int64_t)(intptr_t)rt_lhs->as.val_ptr;
      rhs_s = (int64_t)(intptr_t)rt_rhs->as.val_ptr;
      lhs_u = (uint64_t)(uintptr_t)rt_lhs->as.val_ptr;
      rhs_u = (uint64_t)(uintptr_t)rt_rhs->as.val_ptr;
      break;
    default:
      assert(false && "Invalid type for icmp");
    }
    switch (inst->as.icmp.predicate)
    {
    case IR_ICMP_EQ:
      rt_res->as.val_i1 = (lhs_u == rhs_u);
      break;
    case IR_ICMP_NE:
      rt_res->as.val_i1 = (lhs_u != rhs_u);
      break;
    case IR_ICMP_SGT:
      rt_res->as.val_i1 = (lhs_s > rhs_s);
      break;
    case IR_ICMP_SGE:
      rt_res->as.val_i1 = (lhs_s >= rhs_s);
      break;
    case IR_ICMP_SLT:
      rt_res->as.val_i1 = (lhs_s < rhs_s);
      break;
    case IR_ICMP_SLE:
      rt_res->as.val_i1 = (lhs_s <= rhs_s);
      break;
    case IR_ICMP_UGT:
      rt_res->as.val_i1 = (lhs_u > rhs_u);
      break;
    case IR_ICMP_UGE:
      rt_res->as.val_i1 = (lhs_u >= rhs_u);
      break;
    case IR_ICMP_ULT:
      rt_res->as.val_i1 = (lhs_u < rhs_u);
      break;
    case IR_ICMP_ULE:
      rt_res->as.val_i1 = (lhs_u <= rhs_u);
      break;
    }
    break;
  }
  case IR_OP_FCMP: {

    double lhs = (rt_lhs->kind == RUNTIME_VAL_F32) ? (double)rt_lhs->as.val_f32 : rt_lhs->as.val_f64;
    double rhs = (rt_lhs->kind == RUNTIME_VAL_F32) ? (double)rt_rhs->as.val_f32 : rt_rhs->as.val_f64;

    bool is_uno = isnan(lhs) || isnan(rhs);

    switch (inst->as.fcmp.predicate)
    {

    case IR_FCMP_OEQ:
      rt_res->as.val_i1 = !is_uno && (lhs == rhs);
      break;
    case IR_FCMP_OGT:
      rt_res->as.val_i1 = !is_uno && (lhs > rhs);
      break;
    case IR_FCMP_OGE:
      rt_res->as.val_i1 = !is_uno && (lhs >= rhs);
      break;
    case IR_FCMP_OLT:
      rt_res->as.val_i1 = !is_uno && (lhs < rhs);
      break;
    case IR_FCMP_OLE:
      rt_res->as.val_i1 = !is_uno && (lhs <= rhs);
      break;
    case IR_FCMP_ONE:
      rt_res->as.val_i1 = !is_uno && (lhs != rhs);
      break;

    case IR_FCMP_UEQ:
      rt_res->as.val_i1 = is_uno || (lhs == rhs);
      break;
    case IR_FCMP_UGT:
      rt_res->as.val_i1 = is_uno || (lhs > rhs);
      break;
    case IR_FCMP_UGE:
      rt_res->as.val_i1 = is_uno || (lhs >= rhs);
      break;
    case IR_FCMP_ULT:
      rt_res->as.val_i1 = is_uno || (lhs < rhs);
      break;
    case IR_FCMP_ULE:
      rt_res->as.val_i1 = is_uno || (lhs <= rhs);
      break;
    case IR_FCMP_UNE:
      rt_res->as.val_i1 = is_uno || (lhs != rhs);
      break;

    case IR_FCMP_ORD:
      rt_res->as.val_i1 = !is_uno;
      break;
    case IR_FCMP_UNO:
      rt_res->as.val_i1 = is_uno;
      break;
    case IR_FCMP_TRUE:
      rt_res->as.val_i1 = true;
      break;
    case IR_FCMP_FALSE:
      rt_res->as.val_i1 = false;
      break;
    }
    break;
  }
  default:
    assert(false && "unreachable");
  }

  set_value(ctx, &inst->result, rt_res);
  return EXEC_OK;
}

/**
 * @brief [!!] (已实现) 执行类型转换
 */
static ExecutionResultKind
execute_op_cast(ExecutionContext *ctx, IRInstruction *inst)
{
  RuntimeValue *rt_in = get_value(ctx, get_operand_node(inst, 0));
  RuntimeValue *rt_res = BUMP_ALLOC_ZEROED(&ctx->value_arena, RuntimeValue);

  IRType *dest_type = inst->result.type;
  rt_res->kind = ir_to_runtime_kind(dest_type->kind);

  RuntimeValueKind src_kind = rt_in->kind;

  int64_t src_i64 = 0;
  uint64_t src_u64 = 0;
  double src_f64 = 0.0;
  void *src_ptr = NULL;

  switch (src_kind)
  {
  case RUNTIME_VAL_I1:
    src_i64 = rt_in->as.val_i1;
    src_u64 = rt_in->as.val_i1;
    break;
  case RUNTIME_VAL_I8:
    src_i64 = rt_in->as.val_i8;
    src_u64 = (uint8_t)rt_in->as.val_i8;
    break;
  case RUNTIME_VAL_I16:
    src_i64 = rt_in->as.val_i16;
    src_u64 = (uint16_t)rt_in->as.val_i16;
    break;
  case RUNTIME_VAL_I32:
    src_i64 = rt_in->as.val_i32;
    src_u64 = (uint32_t)rt_in->as.val_i32;
    break;
  case RUNTIME_VAL_I64:
    src_i64 = rt_in->as.val_i64;
    src_u64 = (uint64_t)rt_in->as.val_i64;
    break;
  case RUNTIME_VAL_F32:
    src_f64 = (double)rt_in->as.val_f32;
    break;
  case RUNTIME_VAL_F64:
    src_f64 = rt_in->as.val_f64;
    break;
  case RUNTIME_VAL_PTR:
    src_ptr = rt_in->as.val_ptr;
    src_u64 = (uintptr_t)src_ptr;
    break;
  case RUNTIME_VAL_UNDEF:
    rt_res->kind = RUNTIME_VAL_UNDEF;
    set_value(ctx, &inst->result, rt_res);
    return EXEC_OK;
  }

  switch (inst->opcode)
  {

  case IR_OP_TRUNC:
  case IR_OP_ZEXT:
  case IR_OP_SEXT:

    switch (rt_res->kind)
    {
    case RUNTIME_VAL_I1:
      rt_res->as.val_i1 = (bool)(src_u64 & 1);
      break;
    case RUNTIME_VAL_I8:
      rt_res->as.val_i8 = (int8_t)((inst->opcode == IR_OP_SEXT) ? src_i64 : src_u64);
      break;
    case RUNTIME_VAL_I16:
      rt_res->as.val_i16 = (int16_t)((inst->opcode == IR_OP_SEXT) ? src_i64 : src_u64);
      break;
    case RUNTIME_VAL_I32:
      rt_res->as.val_i32 = (int32_t)((inst->opcode == IR_OP_SEXT) ? src_i64 : src_u64);
      break;
    case RUNTIME_VAL_I64:
      rt_res->as.val_i64 = (int64_t)((inst->opcode == IR_OP_SEXT) ? src_i64 : src_u64);
      break;
    default:
      assert(false && "Invalid integer cast target");
    }
    break;

  case IR_OP_FPTRUNC:
    rt_res->as.val_f32 = (float)src_f64;
    break;
  case IR_OP_FPEXT:
    rt_res->as.val_f64 = (double)src_f64;
    break;

  case IR_OP_FPTOUI:
    switch (rt_res->kind)
    {
    case RUNTIME_VAL_I1:
      rt_res->as.val_i1 = (bool)((uint64_t)src_f64 & 1);
      break;
    case RUNTIME_VAL_I8:
      rt_res->as.val_i8 = (uint8_t)src_f64;
      break;
    case RUNTIME_VAL_I16:
      rt_res->as.val_i16 = (uint16_t)src_f64;
      break;
    case RUNTIME_VAL_I32:
      rt_res->as.val_i32 = (uint32_t)src_f64;
      break;
    case RUNTIME_VAL_I64:
      rt_res->as.val_i64 = (uint64_t)src_f64;
      break;
    default:
      assert(false && "Invalid fptoui target");
    }
    break;
  case IR_OP_FPTOSI:
    switch (rt_res->kind)
    {
    case RUNTIME_VAL_I1:
      rt_res->as.val_i1 = (bool)((int64_t)src_f64 & 1);
      break;
    case RUNTIME_VAL_I8:
      rt_res->as.val_i8 = (int8_t)src_f64;
      break;
    case RUNTIME_VAL_I16:
      rt_res->as.val_i16 = (int16_t)src_f64;
      break;
    case RUNTIME_VAL_I32:
      rt_res->as.val_i32 = (int32_t)src_f64;
      break;
    case RUNTIME_VAL_I64:
      rt_res->as.val_i64 = (int64_t)src_f64;
      break;
    default:
      assert(false && "Invalid fptosi target");
    }
    break;

  case IR_OP_UITOFP:
    if (rt_res->kind == RUNTIME_VAL_F32)
      rt_res->as.val_f32 = (float)src_u64;
    else
      rt_res->as.val_f64 = (double)src_u64;
    break;
  case IR_OP_SITOFP:
    if (rt_res->kind == RUNTIME_VAL_F32)
      rt_res->as.val_f32 = (float)src_i64;
    else
      rt_res->as.val_f64 = (double)src_i64;
    break;

  case IR_OP_PTRTOINT:
    rt_res->as.val_i64 = (int64_t)src_u64;
    break;
  case IR_OP_INTTOPTR:
    rt_res->as.val_ptr = (void *)(uintptr_t)src_u64;
    break;

  case IR_OP_BITCAST:

    assert(datalayout_get_type_size(ctx->interp->data_layout, get_operand_node(inst, 0)->type) ==
             datalayout_get_type_size(ctx->interp->data_layout, dest_type) &&
           "Bitcast size mismatch");
    memcpy(&rt_res->as, &rt_in->as, datalayout_get_type_size(ctx->interp->data_layout, dest_type));
    break;

  default:
    assert(false && "unreachable");
  }

  set_value(ctx, &inst->result, rt_res);
  return EXEC_OK;
}

/*
 * =================================================================
 * --- [!!] (已重构) 核心执行循环 [!!] ---
 * =================================================================
 */

/**
 * @brief [!!] (新增) 辅助函数：将运行时整数值提升为 i64
 */
static int64_t
get_int_value_as_i64(RuntimeValue *rt_val)
{
  switch (rt_val->kind)
  {
  case RUNTIME_VAL_I1:
    return rt_val->as.val_i1;
  case RUNTIME_VAL_I8:
    return rt_val->as.val_i8;
  case RUNTIME_VAL_I16:
    return rt_val->as.val_i16;
  case RUNTIME_VAL_I32:
    return rt_val->as.val_i32;
  case RUNTIME_VAL_I64:
    return rt_val->as.val_i64;
  default:
    assert(false && "Not an integer value");
    return 0;
  }
}

static ExecutionResultKind
execute_basic_block(ExecutionContext *ctx, IRBasicBlock *bb, IRBasicBlock *prev_bb, RuntimeValue *result_out,
                    IRBasicBlock **next_bb_out)
{
  /// 默认情况下，我们不知道下一块是什么
  *next_bb_out = NULL;
  IDList *inst_node;
  list_for_each(&bb->instructions, inst_node)
  {
    IRInstruction *inst = list_entry(inst_node, IRInstruction, list_node);

    switch (inst->opcode)
    {

    case IR_OP_RET: {
      IRValueNode *node_lhs = get_operand_node(inst, 0);
      if (node_lhs)
      {
        RuntimeValue *rt_val = get_value(ctx, node_lhs);
        memcpy(result_out, rt_val, sizeof(RuntimeValue));
      }
      else
      {
        result_out->kind = RUNTIME_VAL_UNDEF;
      }
      *next_bb_out = NULL; /// 信号：停止执行
      return EXEC_OK;
    }
    case IR_OP_BR: {
      IRValueNode *node_lhs = get_operand_node(inst, 0);
      *next_bb_out = container_of(node_lhs, IRBasicBlock, label_address);
      return EXEC_OK;
    }
    case IR_OP_COND_BR: {
      RuntimeValue *rt_val = get_value(ctx, get_operand_node(inst, 0));
      assert(rt_val->kind == RUNTIME_VAL_I1);
      IRValueNode *node_lhs = get_operand_node(inst, (rt_val->as.val_i1) ? 1 : 2);
      *next_bb_out = container_of(node_lhs, IRBasicBlock, label_address);
      return EXEC_OK;
    }
    case IR_OP_SWITCH: {

      RuntimeValue *rt_cond = get_value(ctx, get_operand_node(inst, 0));
      int64_t cond_val = get_int_value_as_i64(rt_cond);

      IRValueNode *dest_node = NULL;

      int op_count = get_operand_count(inst);
      for (int i = 2; i < op_count; i += 2)
      {
        IRValueNode *case_val_node = get_operand_node(inst, i);

        IRConstant *case_const = container_of(case_val_node, IRConstant, value);

        if (case_const->data.int_val == cond_val)
        {
          dest_node = get_operand_node(inst, i + 1);
          break;
        }
      }

      if (dest_node == NULL)
      {
        dest_node = get_operand_node(inst, 1);
      }

      *next_bb_out = container_of(dest_node, IRBasicBlock, label_address);
      return EXEC_OK;
    }

    case IR_OP_ALLOCA: {
      IRType *pointee_type = inst->result.type->as.pointee_type;
      BumpLayout layout = datalayout_get_type_layout(ctx->interp->data_layout, pointee_type);

      void *host_ptr = bump_alloc(&ctx->stack_arena, layout.size, layout.align);

      if (host_ptr == NULL)
      {

        ctx->error_message = "Runtime Error: Stack overflow";
        return EXEC_ERR_STACK_OVERFLOW;
      }

      RuntimeValue *rt_res = BUMP_ALLOC_ZEROED(&ctx->value_arena, RuntimeValue);
      rt_res->kind = RUNTIME_VAL_PTR;
      rt_res->as.val_ptr = host_ptr;
      set_value(ctx, &inst->result, rt_res);

      break;
    }
    case IR_OP_STORE: {
      RuntimeValue *rt_val = get_value(ctx, get_operand_node(inst, 0));
      RuntimeValue *rt_ptr = get_value(ctx, get_operand_node(inst, 1));
      assert(rt_ptr->kind == RUNTIME_VAL_PTR);

      memcpy(rt_ptr->as.val_ptr, &rt_val->as,
             datalayout_get_type_size(ctx->interp->data_layout, get_operand_node(inst, 0)->type));
      break;
    }
    case IR_OP_LOAD: {
      RuntimeValue *rt_ptr = get_value(ctx, get_operand_node(inst, 0));
      assert(rt_ptr->kind == RUNTIME_VAL_PTR);
      RuntimeValue *rt_res = BUMP_ALLOC_ZEROED(&ctx->value_arena, RuntimeValue);
      rt_res->kind = ir_to_runtime_kind(inst->result.type->kind);

      memcpy(&rt_res->as, rt_ptr->as.val_ptr, datalayout_get_type_size(ctx->interp->data_layout, inst->result.type));
      set_value(ctx, &inst->result, rt_res);
      break;
    }
    case IR_OP_GEP: {
      RuntimeValue *rt_base_ptr = get_value(ctx, get_operand_node(inst, 0));
      assert(rt_base_ptr->kind == RUNTIME_VAL_PTR);

      char *current_ptr = (char *)rt_base_ptr->as.val_ptr;

      IRType *current_type = inst->as.gep.source_type;

      int op_count = get_operand_count(inst);
      for (int i = 1; i < op_count; i++)
      {
        RuntimeValue *rt_idx = get_value(ctx, get_operand_node(inst, i));
        int64_t idx_val = get_int_value_as_i64(rt_idx);

        if (i == 1)
        {

          size_t elem_size = datalayout_get_type_size(ctx->interp->data_layout, current_type);
          current_ptr += (idx_val * elem_size);
        }
        else if (current_type->kind == IR_TYPE_ARRAY)
        {

          current_type = current_type->as.array.element_type;
          size_t elem_size = datalayout_get_type_size(ctx->interp->data_layout, current_type);
          current_ptr += (idx_val * elem_size);
        }
        else if (current_type->kind == IR_TYPE_STRUCT)
        {

          assert(idx_val >= 0 && (size_t)idx_val < current_type->as.aggregate.member_count);

          size_t offset = datalayout_get_struct_member_offset(ctx->interp->data_layout, current_type, (size_t)idx_val);
          current_ptr += offset;
          current_type = current_type->as.aggregate.member_types[idx_val];
        }
        else
        {

          assert(false && "GEP is trying to index into a non-aggregate type");
        }
      }

      RuntimeValue *rt_res = BUMP_ALLOC_ZEROED(&ctx->value_arena, RuntimeValue);
      rt_res->kind = RUNTIME_VAL_PTR;
      rt_res->as.val_ptr = (void *)current_ptr;
      set_value(ctx, &inst->result, rt_res);
      break;
    }

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
    case IR_OP_XOR: {
      ExecutionResultKind op_res = execute_op_int_binary(ctx, inst);
      if (op_res != EXEC_OK)
        return op_res;
      break;
    }
    case IR_OP_FADD:
    case IR_OP_FSUB:
    case IR_OP_FMUL:
    case IR_OP_FDIV: {

      ExecutionResultKind op_res = execute_op_float_binary(ctx, inst);
      if (op_res != EXEC_OK)
        return op_res;
      break;
    }
    case IR_OP_ICMP:
    case IR_OP_FCMP:
      execute_op_compare(ctx, inst);
      break;
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
      execute_op_cast(ctx, inst);
      break;

    case IR_OP_PHI: {
      RuntimeValue *rt_val = NULL;
      for (int i = 0; get_operand_node(inst, i) != NULL; i += 2)
      {
        IRValueNode *bb_in_node = get_operand_node(inst, i + 1);
        IRBasicBlock *bb_in = container_of(bb_in_node, IRBasicBlock, label_address);
        if (bb_in == prev_bb)
        {
          IRValueNode *val_in_node = get_operand_node(inst, i);
          rt_val = get_value(ctx, val_in_node);
          break;
        }
      }
      assert(rt_val && "PHI node missing incoming value for predecessor block");
      set_value(ctx, &inst->result, rt_val);
      break;
    }
    case IR_OP_SELECT: {
      ExecutionResultKind op_res = execute_op_select(ctx, inst);
      if (op_res != EXEC_OK)
        return op_res;
      break;
    }
    case IR_OP_CALL: {

      int op_count = get_operand_count(inst);
      size_t num_args = (op_count > 0) ? (op_count - 1) : 0;

      IRValueNode *callee_node = get_operand_node(inst, 0);
      IRFunction *func_to_call = NULL;

      if (callee_node->kind == IR_KIND_FUNCTION)
      {
        func_to_call = container_of(callee_node, IRFunction, entry_address);
      }
      else
      {
        RuntimeValue *rt_callee = get_value(ctx, callee_node);
        assert(rt_callee->kind == RUNTIME_VAL_PTR && "Indirect callee must be a pointer");
        func_to_call = (IRFunction *)rt_callee->as.val_ptr;
      }

      assert(func_to_call && func_to_call->parent != NULL && "Invalid function pointer");

      RuntimeValue **call_args = BUMP_ALLOC_SLICE(&ctx->value_arena, RuntimeValue *, num_args);
      for (size_t i = 0; i < num_args; i++)
      {
        call_args[i] = get_value(ctx, get_operand_node(inst, i + 1));
      }

      RuntimeValue call_result;

      if (func_to_call->is_declaration)
      {

        const char *name = func_to_call->entry_address.name;

        CalicoHostFunction c_func =
          (CalicoHostFunction)str_hashmap_get(ctx->interp->external_function_map, name, strlen(name));

        if (c_func == NULL)
        {

          ctx->error_message = "Runtime Error: Call to unlinked external function";
          return EXEC_ERR_INVALID_PTR;
        }

        ExecutionResultKind ffi_result = c_func(ctx, call_args, num_args, &call_result);

        if (ffi_result != EXEC_OK)
        {

          return ffi_result;
        }
      }
      else
      {

        bool success = interpreter_run_function(ctx->interp, func_to_call, call_args, num_args, &call_result);

        if (!success)
        {

          return EXEC_ERR_INVALID_PTR;
        }
      }

      if (inst->result.type->kind != IR_TYPE_VOID)
      {
        RuntimeValue *rt_res = BUMP_ALLOC(&ctx->value_arena, RuntimeValue);
        memcpy(rt_res, &call_result, sizeof(RuntimeValue));
        set_value(ctx, &inst->result, rt_res);
      }
      break;
    }

    default:
      /// (如果 default 被触发，意味着指令列表为空)
      /// 这不应该发生，因为 Verifier 会确保块有终结者
      break;
    }
  }
  /// 如果循环正常结束，意味着我们执行完了所有指令
  /// 但没有遇到任何一个 "终结者" (RET, BR, ...)，
  /// 这是一个格式错误的 BasicBlock。
  ctx->error_message = "Interpreter Error: Basic block missing terminator";
  return EXEC_ERR_INVALID_PTR;
}

/*
 * =================================================================
 * --- 公共 API (Public API) ---
 * =================================================================
 */

/**
 * @brief 创建一个新的解释器实例。
 * (来自你的 stub)
 */
Interpreter *
interpreter_create(DataLayout *data_layout)
{
  Interpreter *interp = (Interpreter *)malloc(sizeof(Interpreter));
  if (!interp)
    return NULL;

  interp->arena = bump_new();
  if (!interp->arena)
  {
    free(interp);
    return NULL;
  }

  interp->global_storage = ptr_hashmap_create(interp->arena, 64);
  if (!interp->global_storage)
  {
    bump_free(interp->arena);
    free(interp);
    return NULL;
  }

  interp->external_function_map = str_hashmap_create(interp->arena, 64);
  if (!interp->external_function_map)
  {
    bump_free(interp->arena);
    free(interp);
    return NULL;
  }

  /// 存储对 DataLayout 的 *借用*
  interp->data_layout = data_layout;

  return interp;
}

/**
 * @brief 销毁解释器实例并释放其所有内存。
 * (来自你的 stub)
 */
void
interpreter_destroy(Interpreter *interp)
{
  if (!interp)
    return;
  bump_free(interp->arena);
  free(interp);
}

/**
 * @brief [FFI] 注册一个宿主 C 函数，使其可在 IR 中被调用
 * (来自 interpreter.h 的 API 实现)
 */
void
interpreter_register_external_function(Interpreter *interp, const char *name, CalicoHostFunction fn_ptr)
{
  assert(interp != NULL && "Interpreter is NULL");
  assert(name != NULL && "Function name is NULL");
  assert(fn_ptr != NULL && "Function pointer is NULL");

  str_hashmap_put(interp->external_function_map, name, strlen(name), (void *)fn_ptr);
}

/**
 * @brief (已重构) 运行 (解释) 一个 IR 函数。
 */
bool
interpreter_run_function(Interpreter *interp, IRFunction *func, RuntimeValue **args, size_t num_args,
                         RuntimeValue *result_out)
{
  assert(interp && func && result_out && "Invalid arguments for interpreter");
  assert(interp->data_layout != NULL && "Interpreter is missing its DataLayout");

  ExecutionContext ctx;
  ctx.interp = interp;

  bump_init(&ctx.value_arena);
  bump_init(&ctx.stack_arena);
  bump_set_allocation_limit(&ctx.stack_arena, INTERP_STACK_SIZE);

  ctx.frame = ptr_hashmap_create(&ctx.value_arena, 64);
  if (!ctx.frame)
  {
    bump_destroy(&ctx.value_arena);
    bump_destroy(&ctx.stack_arena);
    return false;
  }

  size_t arg_idx = 0;
  IDList *arg_node;
  list_for_each(&func->arguments, arg_node)
  {
    assert(arg_idx < num_args && "Interpreter: Mismatched argument count");
    IRArgument *arg = list_entry(arg_node, IRArgument, list_node);
    set_value(&ctx, &arg->value, args[arg_idx]);
    arg_idx++;
  }

  IRModule *mod = func->parent;
  if (mod)
  {

    IDList *func_it;
    list_for_each(&mod->functions, func_it)
    {
      IRFunction *f = list_entry(func_it, IRFunction, list_node);

      RuntimeValue *rt_func_ptr = BUMP_ALLOC_ZEROED(ctx.interp->arena, RuntimeValue);
      rt_func_ptr->kind = RUNTIME_VAL_PTR;
      rt_func_ptr->as.val_ptr = (void *)f;
      set_value(&ctx, &f->entry_address, rt_func_ptr);
    }

    IDList *global_it;
    list_for_each(&mod->globals, global_it)
    {
      IRGlobalVariable *g = list_entry(global_it, IRGlobalVariable, list_node);
      IRValueNode *global_val_node = &g->value;

      RuntimeValue *rt_ptr = ptr_hashmap_get(interp->global_storage, global_val_node);

      if (rt_ptr == NULL)
      {

        BumpLayout layout = datalayout_get_type_layout(interp->data_layout, g->allocated_type);

        void *host_global_ptr = bump_alloc_layout(ctx.interp->arena, layout);
        assert(host_global_ptr && "OOM Allocating global variable");

        memset(host_global_ptr, 0, layout.size);

        if (g->initializer)
        {

          IRConstant *init_const = container_of(g->initializer, IRConstant, value);

          RuntimeValue *init_val = eval_constant(&ctx, init_const);
          memcpy(host_global_ptr, &init_val->as, layout.size);
        }

        rt_ptr = BUMP_ALLOC_ZEROED(interp->arena, RuntimeValue);
        rt_ptr->kind = RUNTIME_VAL_PTR;
        rt_ptr->as.val_ptr = host_global_ptr;

        ptr_hashmap_put(interp->global_storage, global_val_node, rt_ptr);
      }

      set_value(&ctx, global_val_node, rt_ptr);
    }
  }

  IRBasicBlock *entry_bb = list_entry(func->basic_blocks.next, IRBasicBlock, list_node);
  IRBasicBlock *prev_block = NULL;
  IRBasicBlock *current_block = entry_bb;
  IRBasicBlock *next_block = NULL;

  ctx.error_message = NULL;

  while (current_block)
  {

    ExecutionResultKind bb_result = execute_basic_block(&ctx, current_block, prev_block, result_out, &next_block);

    if (bb_result != EXEC_OK)
    {

      bump_destroy(&ctx.value_arena);
      bump_destroy(&ctx.stack_arena);
      return false;
    }

    if (next_block == NULL)
    {
      break;
    }

    prev_block = current_block;
    current_block = next_block;
  }

  bump_destroy(&ctx.value_arena);
  bump_destroy(&ctx.stack_arena);

  return true;
}
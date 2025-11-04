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

// src/interpreter/interpreter.c
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
#include "utils/hashmap.h"
#include "utils/id_list.h"

#include <assert.h>
#include <math.h> // 用于 fptosi/uitofp 等
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// [!!] 我们为每个函数的模拟栈分配 1MB
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

/**
 * @brief 获取 IRType 在宿主上的大小和*对齐*
 *
 * (这对 GEP 至关重要)
 */
static BumpLayout
get_type_layout(IRType *type)
{
  // [TODO] 这是一个*极其*简化的布局计算。
  // 它假定宿主 (Host) 架构的对齐方式，并且结构体是紧密打包 (packed) 的。
  // 一个真正的解释器需要一个完整的数据布局 (DataLayout) 字符串。
  // 但对于我们的目的，这足够了。

  switch (type->kind)
  {
  case IR_TYPE_I1:
    return (BumpLayout){.size = sizeof(bool), .align = _Alignof(bool)};
  case IR_TYPE_I8:
    return (BumpLayout){.size = sizeof(int8_t), .align = _Alignof(int8_t)};
  case IR_TYPE_I16:
    return (BumpLayout){.size = sizeof(int16_t), .align = _Alignof(int16_t)};
  case IR_TYPE_I32:
    return (BumpLayout){.size = sizeof(int32_t), .align = _Alignof(int32_t)};
  case IR_TYPE_I64:
    return (BumpLayout){.size = sizeof(int64_t), .align = _Alignof(int64_t)};
  case IR_TYPE_F32:
    return (BumpLayout){.size = sizeof(float), .align = _Alignof(float)};
  case IR_TYPE_F64:
    return (BumpLayout){.size = sizeof(double), .align = _Alignof(double)};
  case IR_TYPE_PTR:
    return (BumpLayout){.size = sizeof(void *), .align = _Alignof(void *)};

  case IR_TYPE_ARRAY: {
    BumpLayout elem_layout = get_type_layout(type->as.array.element_type);
    // 数组大小 = 元素大小 * 数量, 对齐 = 元素对齐
    return (BumpLayout){.size = elem_layout.size * type->as.array.element_count, .align = elem_layout.align};
  }

  case IR_TYPE_STRUCT: {
    // [!!] (已修复) 结构体大小计算必须考虑对齐
    size_t total_size = 0;
    size_t max_align = 1;
    for (size_t i = 0; i < type->as.aggregate.member_count; i++)
    {
      BumpLayout member_layout = get_type_layout(type->as.aggregate.member_types[i]);

      // (将 total_size 向上舍入到 member_layout.align 的倍数)
      total_size = (total_size + member_layout.align - 1) & ~(member_layout.align - 1);
      total_size += member_layout.size;

      if (member_layout.align > max_align)
      {
        max_align = member_layout.align;
      }
    }
    // (将总大小向上舍入到最大对齐的倍数)
    total_size = (total_size + max_align - 1) & ~(max_align - 1);
    return (BumpLayout){.size = total_size, .align = max_align};
  }

  default:
    assert(false && "Cannot get layout for complex or void type");
    return (BumpLayout){0, 1};
  }
}

static size_t
get_type_size(IRType *type)
{
  return get_type_layout(type).size;
}

/**
 * @brief [!!] (新增) 辅助函数：计算结构体中成员的对齐后偏移量
 *
 * (这与 get_type_layout 中的逻辑相匹配)
 */
static size_t
get_struct_member_offset(IRType *struct_type, size_t member_idx)
{
  assert(struct_type->kind == IR_TYPE_STRUCT);
  size_t offset = 0;
  for (size_t i = 0; i < member_idx; i++)
  {
    BumpLayout member_layout = get_type_layout(struct_type->as.aggregate.member_types[i]);
    // 1. 将当前偏移量向上舍入到成员的对齐
    offset = (offset + member_layout.align - 1) & ~(member_layout.align - 1);
    // 2. 加上成员的大小
    offset += member_layout.size;
  }

  // 3. 计算第 *member_idx* 个成员的最终偏移量
  BumpLayout final_member_layout = get_type_layout(struct_type->as.aggregate.member_types[member_idx]);
  offset = (offset + final_member_layout.align - 1) & ~(final_member_layout.align - 1);

  return offset;
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
 * @brief [!!] (新增) 执行 'select' 指令
 */
static ExecutionResultKind
execute_op_select(ExecutionContext *ctx, IRInstruction *inst)
{
  RuntimeValue *rt_cond = get_value(ctx, get_operand_node(inst, 0));
  RuntimeValue *rt_true_val = get_value(ctx, get_operand_node(inst, 1));
  RuntimeValue *rt_false_val = get_value(ctx, get_operand_node(inst, 2));

  assert(rt_cond->kind == RUNTIME_VAL_I1 && "Select condition must be i1");

  RuntimeValue *rt_res = (rt_cond->as.val_i1) ? rt_true_val : rt_false_val;

  // PHI 和 SELECT 只是传递值，它们不创建新值，所以我们不需要 BUMP_ALLOC
  set_value(ctx, &inst->result, rt_res);
  return EXEC_OK;
}

/**
 * @brief [!!] (新增) 执行整数/位运算
 */
static ExecutionResultKind
execute_op_int_binary(ExecutionContext *ctx, IRInstruction *inst)
{
  RuntimeValue *rt_lhs = get_value(ctx, get_operand_node(inst, 0));
  RuntimeValue *rt_rhs = get_value(ctx, get_operand_node(inst, 1));
  RuntimeValue *rt_res = BUMP_ALLOC_ZEROED(&ctx->value_arena, RuntimeValue);

  rt_res->kind = rt_lhs->kind;

  // 我们在一个 switch(kind) 中处理所有类型
  // 然后在内部的 switch(opcode) 中处理所有操作
  switch (rt_lhs->kind)
  {
  // --- (为简洁起见，我们将 i1/i8/i16/i32 提升到 i64 来计算) ---
  // [TODO] 一个更精确的实现会分别处理它们以确保正确的溢出/环绕
  case RUNTIME_VAL_I1:
  case RUNTIME_VAL_I8:
  case RUNTIME_VAL_I16:
  case RUNTIME_VAL_I32:
  case RUNTIME_VAL_I64: {
    // 1. 将操作数提升 (promote) 到 64 位
    int64_t lhs = 0, rhs = 0;
    switch (rt_lhs->kind)
    {
    case RUNTIME_VAL_I1:
      lhs = rt_lhs->as.val_i1;
      rhs = rt_rhs->as.val_i1;
      break;
    case RUNTIME_VAL_I8:
      lhs = rt_lhs->as.val_i8;
      rhs = rt_rhs->as.val_i8;
      break;
    case RUNTIME_VAL_I16:
      lhs = rt_lhs->as.val_i16;
      rhs = rt_rhs->as.val_i16;
      break;
    case RUNTIME_VAL_I32:
      lhs = rt_lhs->as.val_i32;
      rhs = rt_rhs->as.val_i32;
      break;
    case RUNTIME_VAL_I64:
      lhs = rt_lhs->as.val_i64;
      rhs = rt_rhs->as.val_i64;
      break;
    default:
      assert(false && "unreachable");
    }

    // 2. 执行运算
    int64_t res = 0;
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
        ctx->error_message = "Runtime Error: Signed division by zero";
        return EXEC_ERR_DIV_BY_ZERO_S; // [!!] 返回错误
      }
      res = lhs / rhs;
      break;
    case IR_OP_SREM:
      if (rhs == 0)
      {
        ctx->error_message = "Runtime Error: Signed remainder by zero";
        return EXEC_ERR_DIV_BY_ZERO_S; // [!!] 返回错误
      }
      res = lhs % rhs;
      break;
    // (对于无符号运算，我们需要将有符号值转为无符号)
    case IR_OP_UDIV:
      if (rhs == 0)
      {
        ctx->error_message = "Runtime Error: Unsigned division by zero";
        return EXEC_ERR_DIV_BY_ZERO_U; // [!!] 返回错误
      }
      res = (int64_t)((uint64_t)lhs / (uint64_t)rhs);
      break;
    case IR_OP_UREM:
      if (rhs == 0)
      {
        ctx->error_message = "Runtime Error: Unsigned remainder by zero";
        return EXEC_ERR_DIV_BY_ZERO_U; // [!!] 返回错误
      }
      res = (int64_t)((uint64_t)lhs % (uint64_t)rhs);
      break;
    // (位运算)
    case IR_OP_SHL:
      res = lhs << rhs;
      break;
    case IR_OP_LSHR:
      res = (int64_t)((uint64_t)lhs >> (uint64_t)rhs);
      break; // 逻辑右移
    case IR_OP_ASHR:
      res = lhs >> rhs;
      break; // 算术右移
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

    // 3. 将 64 位结果写回 (truncate) 到目标类型
    switch (rt_res->kind)
    {
    case RUNTIME_VAL_I1:
      rt_res->as.val_i1 = (bool)(res & 1);
      break;
    case RUNTIME_VAL_I8:
      rt_res->as.val_i8 = (int8_t)res;
      break;
    case RUNTIME_VAL_I16:
      rt_res->as.val_i16 = (int16_t)res;
      break;
    case RUNTIME_VAL_I32:
      rt_res->as.val_i32 = (int32_t)res;
      break;
    case RUNTIME_VAL_I64:
      rt_res->as.val_i64 = (int64_t)res;
      break;
    default:
      assert(false && "unreachable");
    }
    break;
  } // 结束 i64 处理

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

  // (和整数运算一样，我们统一提升到 double (f64) 来计算)
  switch (rt_lhs->kind)
  {
  case RUNTIME_VAL_F32:
  case RUNTIME_VAL_F64: {
    // 1. 提升到 double
    double lhs = (rt_lhs->kind == RUNTIME_VAL_F32) ? (double)rt_lhs->as.val_f32 : rt_lhs->as.val_f64;
    double rhs = (rt_lhs->kind == RUNTIME_VAL_F32) ? (double)rt_rhs->as.val_f32 : rt_rhs->as.val_f64;

    // 2. 执行运算
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

    // 3. 写回目标类型
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

  rt_res->kind = RUNTIME_VAL_I1; // 比较总是返回 i1

  switch (inst->opcode)
  {
  case IR_OP_ICMP: {
    // (ICMP 部分保持不变)
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
    // [!!] (已实现)
    // 1. 提升到 double
    double lhs = (rt_lhs->kind == RUNTIME_VAL_F32) ? (double)rt_lhs->as.val_f32 : rt_lhs->as.val_f64;
    double rhs = (rt_lhs->kind == RUNTIME_VAL_F32) ? (double)rt_rhs->as.val_f32 : rt_rhs->as.val_f64;

    // 2. 检查 NaN (Unordered)
    bool is_uno = isnan(lhs) || isnan(rhs);

    // 3. 执行比较
    switch (inst->as.fcmp.predicate)
    {
    // (Ordered)
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
    // (Unordered)
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
    // (Special)
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

  // 源类型 (用于 switch)
  RuntimeValueKind src_kind = rt_in->kind;

  // (我们再次使用“提升到64位”的技巧)
  int64_t src_i64 = 0;
  uint64_t src_u64 = 0;
  double src_f64 = 0.0;
  void *src_ptr = NULL;

  // 1. 将输入值提取并提升到其“最大”形式
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
    break; // 指针按 u64 处理
  case RUNTIME_VAL_UNDEF:
    rt_res->kind = RUNTIME_VAL_UNDEF; // undef -> undef
    set_value(ctx, &inst->result, rt_res);
    return EXEC_OK;
  }

  // 2. 根据 Opcode 执行转换
  switch (inst->opcode)
  {
  // 整数 -> 整数
  case IR_OP_TRUNC: // 截断 (e.g., i64 -> i8)
  case IR_OP_ZEXT:  // 零扩展 (e.g., i8 -> i64)
  case IR_OP_SEXT:  // 符号扩展 (e.g., i8 -> i64)
    // ZEXT 和 TRUNC 对 u64 操作
    // SEXT 对 i64 操作
    // 注意：ZEXT 和 TRUNC 共享 `src_u64`
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

  // 浮点 -> 浮点
  case IR_OP_FPTRUNC:
    rt_res->as.val_f32 = (float)src_f64;
    break;
  case IR_OP_FPEXT:
    rt_res->as.val_f64 = (double)src_f64;
    break; // (f32 已被提升)

  // 浮点 -> 整数
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
      break; // (u64 -> i64)
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

  // 整数 -> 浮点
  case IR_OP_UITOFP: // 无符号转浮点
    if (rt_res->kind == RUNTIME_VAL_F32)
      rt_res->as.val_f32 = (float)src_u64;
    else
      rt_res->as.val_f64 = (double)src_u64;
    break;
  case IR_OP_SITOFP: // 有符号转浮点
    if (rt_res->kind == RUNTIME_VAL_F32)
      rt_res->as.val_f32 = (float)src_i64;
    else
      rt_res->as.val_f64 = (double)src_i64;
    break;

  // 指针 <-> 整数
  case IR_OP_PTRTOINT:                     // ptr -> u64
    rt_res->as.val_i64 = (int64_t)src_u64; // (已在 src_u64 中)
    break;
  case IR_OP_INTTOPTR: // u64 -> ptr
    rt_res->as.val_ptr = (void *)(uintptr_t)src_u64;
    break;

  case IR_OP_BITCAST:
    // 按位复制 (src 和 dest 必须大小相同, verifier 应该保证)
    assert(get_type_size(get_operand_node(inst, 0)->type) == get_type_size(dest_type) && "Bitcast size mismatch");
    memcpy(&rt_res->as, &rt_in->as, get_type_size(dest_type));
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

    // [!!] --- 主指令 Switch (重构为分发) --- [!!]
    switch (inst->opcode)
    {
    // --- 终结者 ---
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
      *next_bb_out = container_of(node_lhs, IRBasicBlock, label_address); // [!!]
      return EXEC_OK;
    }
    case IR_OP_COND_BR: {
      RuntimeValue *rt_val = get_value(ctx, get_operand_node(inst, 0));
      assert(rt_val->kind == RUNTIME_VAL_I1);
      IRValueNode *node_lhs = get_operand_node(inst, (rt_val->as.val_i1) ? 1 : 2);
      *next_bb_out = container_of(node_lhs, IRBasicBlock, label_address); // [!!]
      return EXEC_OK;
    }
    case IR_OP_SWITCH: {
      // [!!] (已实现)
      RuntimeValue *rt_cond = get_value(ctx, get_operand_node(inst, 0));
      int64_t cond_val = get_int_value_as_i64(rt_cond);

      IRValueNode *dest_node = NULL;

      // (从索引 2 开始遍历 [val, bb] 对)
      int op_count = get_operand_count(inst);
      for (int i = 2; i < op_count; i += 2)
      {
        IRValueNode *case_val_node = get_operand_node(inst, i);
        // 我们知道 case val 必须是常量
        IRConstant *case_const = container_of(case_val_node, IRConstant, value);

        if (case_const->data.int_val == cond_val)
        {
          dest_node = get_operand_node(inst, i + 1);
          break;
        }
      }

      // 如果没有匹配，使用 default (索引 1)
      if (dest_node == NULL)
      {
        dest_node = get_operand_node(inst, 1);
      }

      *next_bb_out = container_of(dest_node, IRBasicBlock, label_address); // [!!]
      return EXEC_OK;
    }

    // --- 内存 ---
    case IR_OP_ALLOCA: {
      IRType *pointee_type = inst->result.type->as.pointee_type;
      BumpLayout layout = get_type_layout(pointee_type);

      void *host_ptr = bump_alloc(&ctx->stack_arena, layout.size, layout.align);

      // [!!] (修复) 检查 bump_alloc 的返回值
      if (host_ptr == NULL)
      {
        // 无论是 OOM 还是达到了 INTERP_STACK_SIZE 限制, bump_alloc 都会失败
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
      // [!!] (已修复) memcpy(&rt_val->as, ...) 是错误的
      memcpy(rt_ptr->as.val_ptr, &rt_val->as, get_type_size(get_operand_node(inst, 0)->type));
      break;
    }
    case IR_OP_LOAD: {
      RuntimeValue *rt_ptr = get_value(ctx, get_operand_node(inst, 0));
      assert(rt_ptr->kind == RUNTIME_VAL_PTR);
      RuntimeValue *rt_res = BUMP_ALLOC_ZEROED(&ctx->value_arena, RuntimeValue);
      rt_res->kind = ir_to_runtime_kind(inst->result.type->kind);
      // [!!] (已修复) memcpy(&rt_res->as, ...) 是错误的
      memcpy(&rt_res->as, rt_ptr->as.val_ptr, get_type_size(inst->result.type));
      set_value(ctx, &inst->result, rt_res);
      break;
    }
    case IR_OP_GEP: {
      RuntimeValue *rt_base_ptr = get_value(ctx, get_operand_node(inst, 0));
      assert(rt_base_ptr->kind == RUNTIME_VAL_PTR);

      char *current_ptr = (char *)rt_base_ptr->as.val_ptr;

      // [!!] (修复) current_type 是 GEP 指令的 "source_type"，
      // 即它指向的第一个元素的类型。
      IRType *current_type = inst->as.gep.source_type;

      int op_count = get_operand_count(inst);
      for (int i = 1; i < op_count; i++)
      {
        RuntimeValue *rt_idx = get_value(ctx, get_operand_node(inst, i));
        int64_t idx_val = get_int_value_as_i64(rt_idx);

        if (i == 1)
        {
          // [!!] (修复) 第一个索引 (i == 1) 总是索引“指针”。
          // 它使用 current_type 的 *大小* 来计算偏移量。
          size_t elem_size = get_type_size(current_type);
          current_ptr += (idx_val * elem_size);

          // 第一个索引计算完偏移量后，current_type 保持不变，
          // 因为我们现在指向了该类型的 *内部* (为 i=2 做准备)。
        }
        else if (current_type->kind == IR_TYPE_ARRAY)
        {
          // [!!] (修复) 后续索引 (i > 1) 索引 *到* 聚合体。
          current_type = current_type->as.array.element_type;
          size_t elem_size = get_type_size(current_type);
          current_ptr += (idx_val * elem_size);
        }
        else if (current_type->kind == IR_TYPE_STRUCT)
        {
          // [!!] (修复) 后续索引 (i > 1) 索引 *到* 聚合体。
          assert(idx_val >= 0 && (size_t)idx_val < current_type->as.aggregate.member_count);

          size_t offset = get_struct_member_offset(current_type, (size_t)idx_val);
          current_ptr += offset;
          current_type = current_type->as.aggregate.member_types[idx_val];
        }
        else
        {
          // Verifier 应该捕获这个 (e.g., "gep %ptr_to_i32, 5, 1")
          assert(false && "GEP is trying to index into a non-aggregate type");
        }
      }

      RuntimeValue *rt_res = BUMP_ALLOC_ZEROED(&ctx->value_arena, RuntimeValue);
      rt_res->kind = RUNTIME_VAL_PTR;
      rt_res->as.val_ptr = (void *)current_ptr;
      set_value(ctx, &inst->result, rt_res);
      break;
    }

    // --- (已实现) 整数/位/浮点/比较/转换 ---
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
      ExecutionResultKind op_res = execute_op_int_binary(ctx, inst); // [!!]
      if (op_res != EXEC_OK)
        return op_res; // [!!] 传播错误
      break;
    }
    case IR_OP_FADD:
    case IR_OP_FSUB:
    case IR_OP_FMUL:
    case IR_OP_FDIV: {
      // [!!] (假设您已重构了 execute_op_float_binary)
      ExecutionResultKind op_res = execute_op_float_binary(ctx, inst);
      if (op_res != EXEC_OK)
        return op_res; // [!!] 传播错误
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

    // --- 其他 ---
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
    case IR_OP_SELECT: { // [!!] (修复)
      ExecutionResultKind op_res = execute_op_select(ctx, inst);
      if (op_res != EXEC_OK)
        return op_res;
      break;
    }
    case IR_OP_CALL: {
      // --- 1. 获取 Callee 和参数 (不变) ---
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

      // --- 2. [!!] FFI 调度 ---
      // 检查我们是在调用 IR 函数 (define) 还是外部 C 函数 (declare)
      if (func_to_call->is_declaration)
      {
        // --- 路径 A: 外部 FFI 调用 ('declare') ---
        const char *name = func_to_call->entry_address.name;

        // 2a. 从 FFI 链接表查找 C ABI 函数指针
        CalicoHostFunction c_func =
          (CalicoHostFunction)str_hashmap_get(ctx->interp->external_function_map, name, strlen(name));

        if (c_func == NULL)
        {
          // IR 是纯粹的 (e.g., 来自 Parser)，但运行时没有链接
          ctx->error_message = "Runtime Error: Call to unlinked external function";
          return EXEC_ERR_INVALID_PTR;
        }

        // 2b. 直接调用 C ABI 包装器
        ExecutionResultKind ffi_result = c_func(ctx, call_args, num_args, &call_result);

        if (ffi_result != EXEC_OK)
        {
          // 外部函数报告了一个运行时错误
          // (c_func 应该已经设置了 ctx->error_message)
          return ffi_result;
        }
      }
      else
      {
        // --- 路径 B: 内部 IR 调用 ('define') ---
        bool success = interpreter_run_function(ctx->interp, func_to_call, call_args, num_args, &call_result);

        if (!success)
        {
          // 递归调用失败 (e.g., 除以零)
          // (error_message 已在递归调用中设置，我们只需传播)
          return EXEC_ERR_INVALID_PTR; // (或使用递归调用返回的特定错误)
        }
      }

      // --- 3. 存储结果 (不变) ---
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
  return EXEC_ERR_INVALID_PTR; // (用一个通用错误代替)
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
interpreter_create(void)
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

  // [!!] (新增) 创建持久的全局存储 Map
  // (我们假设 64 个全局变量足够了)
  interp->global_storage = ptr_hashmap_create(interp->arena, 64);
  if (!interp->global_storage)
  {
    bump_free(interp->arena);
    free(interp);
    return NULL;
  }

  // [!!] (新增) 创建 FFI 外部函数链接表
  // (我们假设 64 个外部函数足够了)
  interp->external_function_map = str_hashmap_create(interp->arena, 64);
  if (!interp->external_function_map)
  {
    bump_free(interp->arena);
    free(interp);
    return NULL;
  }
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

  // 将 'name' (const char*) 作为 void* 键 存入 PtrHashMap
  // (我们假设 'name' 是一个持久的字符串, e.g., C 字符串字面量 "printf")
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

  // --- 1. 初始化执行上下文 ---
  ExecutionContext ctx;
  ctx.interp = interp;

  bump_init(&ctx.value_arena);
  bump_init(&ctx.stack_arena);                                    // [!!] (已修复) 使用 bump_init
  bump_set_allocation_limit(&ctx.stack_arena, INTERP_STACK_SIZE); // [!!] (已修复)

  ctx.frame = ptr_hashmap_create(&ctx.value_arena, 64);
  if (!ctx.frame)
  {
    bump_destroy(&ctx.value_arena);
    bump_destroy(&ctx.stack_arena);
    return false; // OOM
  }

  // --- 2. 加载参数到“寄存器” ---
  size_t arg_idx = 0;
  IDList *arg_node;
  list_for_each(&func->arguments, arg_node)
  {
    assert(arg_idx < num_args && "Interpreter: Mismatched argument count");
    IRArgument *arg = list_entry(arg_node, IRArgument, list_node);
    set_value(&ctx, &arg->value, args[arg_idx]);
    arg_idx++;
  }

  // [!!] (新增) 2.5 将函数和全局变量加载到帧中
  // 这允许 `call @func` 和 `load @global` 工作
  IRModule *mod = func->parent;
  if (mod)
  {
    // [!!] (修复) 2.5 仅循环一次函数
    IDList *func_it;
    list_for_each(&mod->functions, func_it)
    {
      IRFunction *f = list_entry(func_it, IRFunction, list_node);
      // [!!] (已确认) 这是正确的。
      // 我们将 @func (f->entry_address) 映射到
      // 一个值为 (void*)f 的 RuntimeValue。
      RuntimeValue *rt_func_ptr = BUMP_ALLOC_ZEROED(ctx.interp->arena, RuntimeValue);
      rt_func_ptr->kind = RUNTIME_VAL_PTR;
      rt_func_ptr->as.val_ptr = (void *)f;
      set_value(&ctx, &f->entry_address, rt_func_ptr);
    } // [!!] (修复) 函数循环在这里结束

    // [!!] (修复) 2.6 将全局变量循环移到函数循环 *之外*
    IDList *global_it;
    list_for_each(&mod->globals, global_it)
    {
      IRGlobalVariable *g = list_entry(global_it, IRGlobalVariable, list_node);
      IRValueNode *global_val_node = &g->value;

      // 1. 查找这个全局变量是否已经有内存了
      RuntimeValue *rt_ptr = ptr_hashmap_get(interp->global_storage, global_val_node);

      if (rt_ptr == NULL)
      {
        // 2. (首次运行) 分配持久内存 (在 *interp* arena)
        BumpLayout layout = get_type_layout(g->allocated_type);

        // [!!] (修复) 必须使用 bump_alloc_layout 来获取正确的大小和对齐
        void *host_global_ptr = bump_alloc_layout(ctx.interp->arena, layout);
        assert(host_global_ptr && "OOM Allocating global variable");

        // [!!] (修复) 手动清零 (BUMP_ALLOC_ZEROED/SLICE_ZEROED 不适用这里)
        memset(host_global_ptr, 0, layout.size);

        // 2a. 处理 *非* zero 的初始化
        if (g->initializer)
        {
          // [!!] (修复) g->initializer 是 IRValueNode* (基类)
          // Verifier 保证了它必须是 IR_KIND_CONSTANT。
          // 我们需要使用 container_of 转回 IRConstant* (容器)。
          IRConstant *init_const = container_of(g->initializer, IRConstant, value);

          // (我们必须使用 *当前* ctx 来评估常量)
          RuntimeValue *init_val = eval_constant(&ctx, init_const);
          memcpy(host_global_ptr, &init_val->as, layout.size);
        }

        // 2b. 为 *指针* 创建一个持久的 RuntimeValue
        rt_ptr = BUMP_ALLOC_ZEROED(interp->arena, RuntimeValue);
        rt_ptr->kind = RUNTIME_VAL_PTR;
        rt_ptr->as.val_ptr = host_global_ptr;

        // 2c. 将其存储在持久的全局 map 中
        ptr_hashmap_put(interp->global_storage, global_val_node, rt_ptr);
      }

      // 3. 将这个持久的指针值设置到 *当前帧*
      set_value(&ctx, global_val_node, rt_ptr);
    }
  }

  // --- 3. 执行 ---
  IRBasicBlock *entry_bb = list_entry(func->basic_blocks.next, IRBasicBlock, list_node);
  IRBasicBlock *prev_block = NULL;
  IRBasicBlock *current_block = entry_bb;
  IRBasicBlock *next_block = NULL; // [!!] (新增)

  ctx.error_message = NULL; // [!!] (新增) 初始化

  while (current_block)
  {
    // [!!] (重构)
    ExecutionResultKind bb_result = execute_basic_block(&ctx, current_block, prev_block, result_out, &next_block);

    if (bb_result != EXEC_OK)
    {
      // [!!] (新增) 发生运行时错误
      // 错误信息已在 ctx.error_message 中设置
      // 我们可以选择将它存储在 interp 中供调用者检查
      // interp->last_error = ctx.error_message;

      // --- 4. 清理 (失败路径) ---
      bump_destroy(&ctx.value_arena);
      bump_destroy(&ctx.stack_arena);
      return false; // [!!] (按 API 约定返回 false)
    }

    // (检查是否遇到了 'ret'，由 next_block == NULL 信号)
    if (next_block == NULL)
    {
      break; // 正常退出
    }

    prev_block = current_block;
    current_block = next_block;
  }

  // --- 4. 清理 ---
  bump_destroy(&ctx.value_arena);
  bump_destroy(&ctx.stack_arena);

  return true;
}
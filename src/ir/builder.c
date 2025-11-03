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

#include "ir/builder.h"
#include "ir/basicblock.h"
#include "ir/constant.h"
#include "ir/context.h"
#include "ir/function.h"
#include "ir/instruction.h"
#include "ir/type.h"
#include "ir/use.h"
#include "ir/value.h"
#include "utils/bump.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

IRBuilder *
ir_builder_create(IRContext *ctx)
{
  assert(ctx != NULL);

  IRBuilder *builder = (IRBuilder *)malloc(sizeof(IRBuilder));
  if (!builder)
    return NULL;

  builder->context = ctx;
  builder->insertion_point = NULL;
  builder->next_temp_reg_id = 0;
  return builder;
}

void
ir_builder_destroy(IRBuilder *builder)
{
  if (!builder)
    return;
  free(builder);
}

void
ir_builder_set_insertion_point(IRBuilder *builder, IRBasicBlock *bb)
{
  assert(builder != NULL);
  builder->insertion_point = bb;
}

/*
 * =================================================================
 * --- 内部辅助函数 ---
 * =================================================================
 */

/**
 * @brief [内部] 生成下一个临时寄存器名 (e.g., "%1")
 *
 * 名字被分配在 *IR Arena* 中 (因为它们被 IRInstruction 引用)
 *
 * @param builder Builder
 * @return const char* 指向 Arena 中的字符串
 */
static const char *
builder_get_next_reg_name(IRBuilder *builder)
{
  IRContext *ctx = builder->context;
  char buffer[16];

  snprintf(buffer, sizeof(buffer), "%zu", builder->next_temp_reg_id);
  builder->next_temp_reg_id++;

  return ir_context_intern_str(ctx, buffer);
}

/**
 * @brief [内部] 分配并初始化指令 (但不创建 Operands)
 * @param builder Builder
 * @param opcode 指令码
 * @param type 指令*结果*的类型 (如果是 void, 使用 ctx->type_void)
 * @return 指向新指令的指针
 */
static IRInstruction *
ir_instruction_create_internal(IRBuilder *builder, IROpcode opcode, IRType *type, const char *name_hint)
{
  assert(builder != NULL);
  assert(builder->insertion_point != NULL && "Builder insertion point is not set");
  IRContext *ctx = builder->context;

  IRInstruction *inst = BUMP_ALLOC_ZEROED(&ctx->ir_arena, IRInstruction);
  if (!inst)
    return NULL;

  inst->result.kind = IR_KIND_INSTRUCTION;
  inst->result.type = type;
  list_init(&inst->result.uses);

  inst->opcode = opcode;
  inst->parent = builder->insertion_point;
  list_init(&inst->list_node);
  list_init(&inst->operands);

  if (type->kind != IR_TYPE_VOID)
  {
    if (name_hint)
    {

      inst->result.name = ir_context_intern_str(ctx, name_hint);
    }
    else
    {

      inst->result.name = builder_get_next_reg_name(builder);
    }
  }
  else
  {
    inst->result.name = NULL;
  }

  list_add_tail(&builder->insertion_point->instructions, &inst->list_node);

  return inst;
}

/*
 * =================================================================
 * --- 公共 API 实现 ---
 * =================================================================
 */

IRValueNode *
ir_builder_create_ret(IRBuilder *builder, IRValueNode *val)
{
  IRType *void_type = builder->context->type_void;
  IRInstruction *inst = ir_instruction_create_internal(builder, IR_OP_RET, void_type, NULL);

  if (val)
  {

    ir_use_create(builder->context, inst, val);
  }

  return &inst->result;
}

IRValueNode *
ir_builder_create_br(IRBuilder *builder, IRValueNode *target_bb)
{
  assert(target_bb != NULL);
  assert(target_bb->kind == IR_KIND_BASIC_BLOCK && "br target must be a Basic Block");

  IRType *void_type = builder->context->type_void;
  IRInstruction *inst = ir_instruction_create_internal(builder, IR_OP_BR, void_type, NULL);

  ir_use_create(builder->context, inst, target_bb);

  return &inst->result;
}

IRValueNode *
ir_builder_create_cond_br(IRBuilder *builder, IRValueNode *cond, IRValueNode *true_bb, IRValueNode *false_bb)
{
  assert(builder != NULL);
  assert(cond != NULL && true_bb != NULL && false_bb != NULL);

  assert(cond->type == ir_type_get_i1(builder->context) && "br condition must be i1");
  assert(true_bb->kind == IR_KIND_BASIC_BLOCK && "br target must be a label");
  assert(false_bb->kind == IR_KIND_BASIC_BLOCK && "br target must be a label");

  IRType *void_type = builder->context->type_void;

  IRInstruction *inst = ir_instruction_create_internal(builder, IR_OP_COND_BR, void_type, NULL);

  if (!inst)
    return NULL;

  ir_use_create(builder->context, inst, cond);
  ir_use_create(builder->context, inst, true_bb);
  ir_use_create(builder->context, inst, false_bb);

  return &inst->result;
}

static IRValueNode *
builder_create_binary_op(IRBuilder *builder, IROpcode op, IRValueNode *lhs, IRValueNode *rhs, const char *name_hint)
{
  assert(lhs != NULL && rhs != NULL);
  assert(lhs->type == rhs->type && "Binary operands must have the same type");

  IRInstruction *inst = ir_instruction_create_internal(builder, op, lhs->type, name_hint);

  ir_use_create(builder->context, inst, lhs);
  ir_use_create(builder->context, inst, rhs);

  return &inst->result;
}

/**
 * @brief (内部) 构建单操作数的类型转换指令
 */
static IRValueNode *
builder_create_cast_op(IRBuilder *builder, IROpcode op, IRValueNode *val, IRType *dest_type, const char *name_hint)
{
  assert(builder != NULL);
  assert(val != NULL);
  assert(dest_type != NULL && "Cast destination type cannot be NULL");

  IRInstruction *inst = ir_instruction_create_internal(builder, op, dest_type, name_hint);
  if (!inst)
    return NULL;

  ir_use_create(builder->context, inst, val);

  return &inst->result;
}

IRValueNode *
ir_builder_create_add(IRBuilder *builder, IRValueNode *lhs, IRValueNode *rhs, const char *name_hint)
{
  return builder_create_binary_op(builder, IR_OP_ADD, lhs, rhs, name_hint);
}

IRValueNode *
ir_builder_create_sub(IRBuilder *builder, IRValueNode *lhs, IRValueNode *rhs, const char *name_hint)
{
  return builder_create_binary_op(builder, IR_OP_SUB, lhs, rhs, name_hint);
}

IRValueNode *
ir_builder_create_mul(IRBuilder *builder, IRValueNode *lhs, IRValueNode *rhs, const char *name_hint)
{
  return builder_create_binary_op(builder, IR_OP_MUL, lhs, rhs, name_hint);
}

IRValueNode *
ir_builder_create_udiv(IRBuilder *builder, IRValueNode *lhs, IRValueNode *rhs, const char *name_hint)
{
  return builder_create_binary_op(builder, IR_OP_UDIV, lhs, rhs, name_hint);
}

IRValueNode *
ir_builder_create_sdiv(IRBuilder *builder, IRValueNode *lhs, IRValueNode *rhs, const char *name_hint)
{
  return builder_create_binary_op(builder, IR_OP_SDIV, lhs, rhs, name_hint);
}

IRValueNode *
ir_builder_create_urem(IRBuilder *builder, IRValueNode *lhs, IRValueNode *rhs, const char *name_hint)
{
  return builder_create_binary_op(builder, IR_OP_UREM, lhs, rhs, name_hint);
}

IRValueNode *
ir_builder_create_srem(IRBuilder *builder, IRValueNode *lhs, IRValueNode *rhs, const char *name_hint)
{
  return builder_create_binary_op(builder, IR_OP_SREM, lhs, rhs, name_hint);
}

// --- [!!] 新增：浮点二元运算 ---

IRValueNode *
ir_builder_create_fadd(IRBuilder *builder, IRValueNode *lhs, IRValueNode *rhs, const char *name_hint)
{
  return builder_create_binary_op(builder, IR_OP_FADD, lhs, rhs, name_hint);
}

IRValueNode *
ir_builder_create_fsub(IRBuilder *builder, IRValueNode *lhs, IRValueNode *rhs, const char *name_hint)
{
  return builder_create_binary_op(builder, IR_OP_FSUB, lhs, rhs, name_hint);
}

IRValueNode *
ir_builder_create_fmul(IRBuilder *builder, IRValueNode *lhs, IRValueNode *rhs, const char *name_hint)
{
  return builder_create_binary_op(builder, IR_OP_FMUL, lhs, rhs, name_hint);
}

IRValueNode *
ir_builder_create_fdiv(IRBuilder *builder, IRValueNode *lhs, IRValueNode *rhs, const char *name_hint)
{
  return builder_create_binary_op(builder, IR_OP_FDIV, lhs, rhs, name_hint);
}

// --- [!!] 新增：位运算 ---

IRValueNode *
ir_builder_create_shl(IRBuilder *builder, IRValueNode *lhs, IRValueNode *rhs, const char *name_hint)
{
  return builder_create_binary_op(builder, IR_OP_SHL, lhs, rhs, name_hint);
}

IRValueNode *
ir_builder_create_lshr(IRBuilder *builder, IRValueNode *lhs, IRValueNode *rhs, const char *name_hint)
{
  return builder_create_binary_op(builder, IR_OP_LSHR, lhs, rhs, name_hint);
}

IRValueNode *
ir_builder_create_ashr(IRBuilder *builder, IRValueNode *lhs, IRValueNode *rhs, const char *name_hint)
{
  return builder_create_binary_op(builder, IR_OP_ASHR, lhs, rhs, name_hint);
}

IRValueNode *
ir_builder_create_and(IRBuilder *builder, IRValueNode *lhs, IRValueNode *rhs, const char *name_hint)
{
  return builder_create_binary_op(builder, IR_OP_AND, lhs, rhs, name_hint);
}

IRValueNode *
ir_builder_create_or(IRBuilder *builder, IRValueNode *lhs, IRValueNode *rhs, const char *name_hint)
{
  return builder_create_binary_op(builder, IR_OP_OR, lhs, rhs, name_hint);
}

IRValueNode *
ir_builder_create_xor(IRBuilder *builder, IRValueNode *lhs, IRValueNode *rhs, const char *name_hint)
{
  return builder_create_binary_op(builder, IR_OP_XOR, lhs, rhs, name_hint);
}

IRValueNode *
ir_builder_create_icmp(IRBuilder *builder, IRICmpPredicate pred, IRValueNode *lhs, IRValueNode *rhs,
                       const char *name_hint)
{
  assert(builder != NULL);
  assert(lhs != NULL && rhs != NULL);
  assert(lhs->type == rhs->type && "ICMP operands must have the same type");

  IRType *result_type = ir_type_get_i1(builder->context);

  IRInstruction *inst = ir_instruction_create_internal(builder, IR_OP_ICMP, result_type, name_hint);

  if (!inst)
    return NULL;

  inst->as.icmp.predicate = pred;

  ir_use_create(builder->context, inst, lhs);
  ir_use_create(builder->context, inst, rhs);

  return &inst->result;
}

IRValueNode *
ir_builder_create_fcmp(IRBuilder *builder, IRFCmpPredicate pred, IRValueNode *lhs, IRValueNode *rhs,
                       const char *name_hint)
{
  assert(builder != NULL);
  assert(lhs != NULL && rhs != NULL);
  assert(lhs->type == rhs->type && "FCMP operands must have the same type");
  assert(ir_type_is_floating(lhs->type) && "FCMP operands must be floating point");

  IRType *result_type = ir_type_get_i1(builder->context);

  IRInstruction *inst = ir_instruction_create_internal(builder, IR_OP_FCMP, result_type, name_hint);

  if (!inst)
    return NULL;

  inst->as.fcmp.predicate = pred; // [!!] 注意：使用 fcmp 联合成员

  ir_use_create(builder->context, inst, lhs);
  ir_use_create(builder->context, inst, rhs);

  return &inst->result;
}

IRValueNode *
ir_builder_create_alloca(IRBuilder *builder, IRType *allocated_type, const char *name_hint)
{
  assert(allocated_type != NULL);
  IRContext *ctx = builder->context;

  IRType *ptr_type = ir_type_get_ptr(ctx, allocated_type);

  IRInstruction *inst = ir_instruction_create_internal(builder, IR_OP_ALLOCA, ptr_type, name_hint);

  return &inst->result;
}

IRValueNode *
ir_builder_create_load(IRBuilder *builder, IRValueNode *ptr, const char *name_hint)
{
  assert(ptr != NULL);
  assert(ptr->type->kind == IR_TYPE_PTR && "load operand must be a pointer");

  IRType *result_type = ptr->type->as.pointee_type;
  assert(result_type != NULL);

  IRInstruction *inst = ir_instruction_create_internal(builder, IR_OP_LOAD, result_type, name_hint);
  ir_use_create(builder->context, inst, ptr);

  return &inst->result;
}

IRValueNode *
ir_builder_create_store(IRBuilder *builder, IRValueNode *val, IRValueNode *ptr)
{
  assert(val != NULL);
  assert(ptr != NULL);
  assert(ptr->type->kind == IR_TYPE_PTR && "store target must be a pointer");

  IRType *void_type = builder->context->type_void;
  IRInstruction *inst = ir_instruction_create_internal(builder, IR_OP_STORE, void_type, NULL);

  ir_use_create(builder->context, inst, val);
  ir_use_create(builder->context, inst, ptr);

  return &inst->result;
}

IRValueNode *
ir_builder_create_phi(IRBuilder *builder, IRType *type, const char *name_hint)
{
  assert(builder != NULL);
  assert(builder->insertion_point != NULL && "Builder insertion point is not set");
  assert(type != NULL && type->kind != IR_TYPE_VOID && "PHI type cannot be void");

  IRContext *ctx = builder->context;

  IRInstruction *inst = BUMP_ALLOC_ZEROED(&ctx->ir_arena, IRInstruction);
  if (!inst)
    return NULL;

  inst->result.kind = IR_KIND_INSTRUCTION;
  inst->result.type = type;
  list_init(&inst->result.uses);

  inst->opcode = IR_OP_PHI;
  inst->parent = builder->insertion_point;
  list_init(&inst->list_node);
  list_init(&inst->operands);

  if (name_hint)
  {
    inst->result.name = ir_context_intern_str(ctx, name_hint);
  }
  else
  {
    inst->result.name = builder_get_next_reg_name(builder);
  }

  list_add(&builder->insertion_point->instructions, &inst->list_node);

  return &inst->result;
}

void
ir_phi_add_incoming(IRValueNode *phi_node, IRValueNode *value, IRBasicBlock *incoming_bb)
{
  assert(phi_node != NULL && phi_node->kind == IR_KIND_INSTRUCTION);
  assert(value != NULL);
  assert(incoming_bb != NULL && incoming_bb->label_address.kind == IR_KIND_BASIC_BLOCK);

  IRInstruction *inst = (IRInstruction *)phi_node;
  assert(inst->opcode == IR_OP_PHI && "Value is not a PHI node");
  assert(inst->result.type == value->type && "Incoming value type mismatch PHI type");

  assert(inst->parent != NULL && inst->parent->parent != NULL && inst->parent->parent->parent != NULL);
  IRContext *ctx = inst->parent->parent->parent->context;

  ir_use_create(ctx, inst, value);
  ir_use_create(ctx, inst, &incoming_bb->label_address);
}

/**
 * @brief [内部] GEP 辅助函数：从 IR_KIND_CONSTANT 中提取整数值。
 *
 * @param constant_val 必须是一个 IR_KIND_CONSTANT 的 IRValueNode*
 * @param out_value    [out] 用于存储提取出的 64 位无符号值
 * @return true 如果成功 (是常量且是整数), false 否则
 */
static bool
gep_get_constant_index(IRValueNode *constant_val, uint64_t *out_value)
{
  assert(constant_val != NULL);
  if (constant_val->kind != IR_KIND_CONSTANT)
  {
    return false;
  }

  IRConstant *k = (IRConstant *)constant_val;

  if (k->const_kind != CONST_KIND_INT)
  {
    return false;
  }

  *out_value = (uint64_t)k->data.int_val;
  return true;
}

IRValueNode *
ir_builder_create_gep(IRBuilder *builder, IRType *source_type, IRValueNode *base_ptr, IRValueNode **indices,
                      size_t num_indices, bool inbounds, const char *name_hint)
{
  assert(builder != NULL);
  assert(source_type != NULL);
  assert(base_ptr != NULL && base_ptr->type->kind == IR_TYPE_PTR);
  assert(indices != NULL || num_indices == 0);

  IRContext *ctx = builder->context;

  IRType *current_type = source_type;

  for (size_t i = 1; i < num_indices; i++)
  {
    IRValueNode *index_val = indices[i];

    switch (current_type->kind)
    {
    case IR_TYPE_ARRAY:

      current_type = current_type->as.array.element_type;
      break;

    case IR_TYPE_STRUCT: {

      uint64_t member_idx = 0;
      bool is_const = gep_get_constant_index(index_val, &member_idx);

      assert(is_const && "GEP index into a struct must be a constant integer");
      assert(member_idx < current_type->as.aggregate.member_count && "GEP struct index out of bounds");

      current_type = current_type->as.aggregate.member_types[member_idx];
      break;
    }

    default:

      assert(0 && "GEP trying to index into a non-aggregate type");
      break;
    }
  }

  IRType *result_type = ir_type_get_ptr(ctx, current_type);

  IRInstruction *inst = ir_instruction_create_internal(builder, IR_OP_GEP, result_type, name_hint);

  if (!inst)
    return NULL;

  inst->as.gep.source_type = source_type;
  inst->as.gep.inbounds = inbounds;

  ir_use_create(ctx, inst, base_ptr);
  for (size_t i = 0; i < num_indices; i++)
  {
    ir_use_create(ctx, inst, indices[i]);
  }

  return &inst->result;
}

IRValueNode *
ir_builder_create_call(IRBuilder *builder, IRValueNode *callee_func, IRValueNode **args, size_t num_args,
                       const char *name_hint)
{
  assert(builder != NULL);
  assert(callee_func != NULL);
  assert(args != NULL || num_args == 0);

  IRType *callee_type = callee_func->type;
  assert(callee_type->kind == IR_TYPE_PTR && "callee must be a pointer type");

  IRType *func_type = callee_type->as.pointee_type;
  assert(func_type->kind == IR_TYPE_FUNCTION && "callee must be a pointer to a function type");

  IRType *result_type = func_type->as.function.return_type;

  const bool is_variadic = func_type->as.function.is_variadic;
  const size_t required_params = func_type->as.function.param_count;

  const bool variadic_ok = is_variadic && (num_args >= required_params);

  const bool non_variadic_ok = !is_variadic && (num_args == required_params);

  const bool is_valid_arg_count = variadic_ok || non_variadic_ok;

  assert(is_valid_arg_count && "call argument count mismatch");

  IRInstruction *inst = ir_instruction_create_internal(builder, IR_OP_CALL, result_type, name_hint);
  if (!inst)
    return NULL;

  ir_use_create(builder->context, inst, callee_func);

  for (size_t i = 0; i < num_args; i++)
  {
    ir_use_create(builder->context, inst, args[i]);
  }

  return &inst->result;
}

IRValueNode *
ir_builder_create_trunc(IRBuilder *builder, IRValueNode *val, IRType *dest_type, const char *name_hint)
{
  return builder_create_cast_op(builder, IR_OP_TRUNC, val, dest_type, name_hint);
}

IRValueNode *
ir_builder_create_zext(IRBuilder *builder, IRValueNode *val, IRType *dest_type, const char *name_hint)
{
  return builder_create_cast_op(builder, IR_OP_ZEXT, val, dest_type, name_hint);
}

IRValueNode *
ir_builder_create_sext(IRBuilder *builder, IRValueNode *val, IRType *dest_type, const char *name_hint)
{
  return builder_create_cast_op(builder, IR_OP_SEXT, val, dest_type, name_hint);
}

IRValueNode *
ir_builder_create_fptrunc(IRBuilder *builder, IRValueNode *val, IRType *dest_type, const char *name_hint)
{
  return builder_create_cast_op(builder, IR_OP_FPTRUNC, val, dest_type, name_hint);
}

IRValueNode *
ir_builder_create_fpext(IRBuilder *builder, IRValueNode *val, IRType *dest_type, const char *name_hint)
{
  return builder_create_cast_op(builder, IR_OP_FPEXT, val, dest_type, name_hint);
}

IRValueNode *
ir_builder_create_fptoui(IRBuilder *builder, IRValueNode *val, IRType *dest_type, const char *name_hint)
{
  return builder_create_cast_op(builder, IR_OP_FPTOUI, val, dest_type, name_hint);
}

IRValueNode *
ir_builder_create_fptosi(IRBuilder *builder, IRValueNode *val, IRType *dest_type, const char *name_hint)
{
  return builder_create_cast_op(builder, IR_OP_FPTOSI, val, dest_type, name_hint);
}

IRValueNode *
ir_builder_create_uitofp(IRBuilder *builder, IRValueNode *val, IRType *dest_type, const char *name_hint)
{
  return builder_create_cast_op(builder, IR_OP_UITOFP, val, dest_type, name_hint);
}

IRValueNode *
ir_builder_create_sitofp(IRBuilder *builder, IRValueNode *val, IRType *dest_type, const char *name_hint)
{
  return builder_create_cast_op(builder, IR_OP_SITOFP, val, dest_type, name_hint);
}

IRValueNode *
ir_builder_create_ptrtoint(IRBuilder *builder, IRValueNode *val, IRType *dest_type, const char *name_hint)
{
  return builder_create_cast_op(builder, IR_OP_PTRTOINT, val, dest_type, name_hint);
}

IRValueNode *
ir_builder_create_inttoptr(IRBuilder *builder, IRValueNode *val, IRType *dest_type, const char *name_hint)
{
  return builder_create_cast_op(builder, IR_OP_INTTOPTR, val, dest_type, name_hint);
}

IRValueNode *
ir_builder_create_bitcast(IRBuilder *builder, IRValueNode *val, IRType *dest_type, const char *name_hint)
{
  return builder_create_cast_op(builder, IR_OP_BITCAST, val, dest_type, name_hint);
}

IRValueNode *
ir_builder_create_switch(IRBuilder *builder, IRValueNode *cond, IRValueNode *default_bb)
{
  assert(builder != NULL);
  assert(cond != NULL);
  assert(default_bb != NULL && default_bb->kind == IR_KIND_BASIC_BLOCK);
  assert(ir_type_is_integer(cond->type) && "Switch condition must be an integer");

  IRType *void_type = builder->context->type_void;
  IRInstruction *inst = ir_instruction_create_internal(builder, IR_OP_SWITCH, void_type, NULL);
  if (!inst)
    return NULL;

  // [!!] 遵循我们的"选项1"
  // 操作数 0: cond
  // 操作数 1: default_bb
  ir_use_create(builder->context, inst, cond);
  ir_use_create(builder->context, inst, default_bb);

  return &inst->result;
}

void
ir_switch_add_case(IRValueNode *switch_inst_val, IRValueNode *const_val, IRValueNode *target_bb)
{
  assert(switch_inst_val != NULL && switch_inst_val->kind == IR_KIND_INSTRUCTION);
  assert(const_val != NULL && const_val->kind == IR_KIND_CONSTANT);
  assert(target_bb != NULL && target_bb->kind == IR_KIND_BASIC_BLOCK);

  IRInstruction *inst = (IRInstruction *)switch_inst_val;
  assert(inst->opcode == IR_OP_SWITCH && "Value is not a Switch instruction");

  // 从指令反向获取 Context (复用 phi_add_incoming 的逻辑)
  assert(inst->parent != NULL && inst->parent->parent != NULL && inst->parent->parent->parent != NULL);
  IRContext *ctx = inst->parent->parent->parent->context;

  // [!!] 遵循我们的"选项1"
  // 将 [val, bb] 对追加到 operands 列表的末尾
  ir_use_create(ctx, inst, const_val);
  ir_use_create(ctx, inst, target_bb);
}
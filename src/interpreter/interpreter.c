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
#include "ir/instruction.h"
#include "ir/type.h"
#include "ir/use.h"
#include "ir/value.h"

#include "utils/bump.h"
#include "utils/hashmap.h"
#include "utils/id_list.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct HostAllocation
{
  void *ptr;
  IDList list_node;
} HostAllocation;

/**
 * @brief [辅助] 获取指令的第 N 个操作数 (IRValueNode*)
 */
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

/**
 * @brief [辅助] 将 IRTypeKind 转换为 RuntimeValueKind
 */
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
 * @brief [辅助] 获取 IRType 在宿主上的大小 (用于 alloca/load/store)
 */
static size_t
get_type_size(IRType *type)
{
  switch (type->kind)
  {
  case IR_TYPE_I1:
    return sizeof(bool);
  case IR_TYPE_I8:
    return sizeof(int8_t);
  case IR_TYPE_I16:
    return sizeof(int16_t);
  case IR_TYPE_I32:
    return sizeof(int32_t);
  case IR_TYPE_I64:
    return sizeof(int64_t);
  case IR_TYPE_F32:
    return sizeof(float);
  case IR_TYPE_F64:
    return sizeof(double);
  case IR_TYPE_PTR:
    return sizeof(void *);

  default:
    assert(false && "Cannot get size for unknown type");
    return 0;
  }
}

/**
 * @brief [核心] 将 IRConstant 转换为 RuntimeValue
 */
static RuntimeValue *
eval_constant(Interpreter *interp, PtrHashMap *frame, IRConstant *constant)
{
  IRValueNode *val_node = &constant->value;

  RuntimeValue *rt_val = BUMP_ALLOC_ZEROED(interp->arena, RuntimeValue);

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

  ptr_hashmap_put(frame, val_node, rt_val);
  return rt_val;
}

/**
 * @brief [核心] 获取一个 IRValueNode* 对应的 RuntimeValue*
 */
static RuntimeValue *
get_value(Interpreter *interp, PtrHashMap *frame, IRValueNode *val_node)
{

  RuntimeValue *rt_val = ptr_hashmap_get(frame, val_node);
  if (rt_val)
  {
    return rt_val;
  }

  if (val_node->kind == IR_KIND_CONSTANT)
  {
    IRConstant *constant = container_of(val_node, IRConstant, value);
    return eval_constant(interp, frame, constant);
  }

  return NULL;
}

/**
 * @brief [核心] 将一个 RuntimeValue* 存入执行帧
 */
static void
set_value(PtrHashMap *frame, IRValueNode *val_node, RuntimeValue *rt_val)
{
  ptr_hashmap_put(frame, val_node, rt_val);
}

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
  return interp;
}

void
interpreter_destroy(Interpreter *interp)
{
  if (!interp)
    return;
  bump_free(interp->arena);
  free(interp);
}

bool
interpreter_run_function(Interpreter *interp, IRFunction *func, RuntimeValue **args, size_t num_args,
                         RuntimeValue *result_out)
{
  assert(interp && func && result_out && "Invalid arguments for interpreter");

  PtrHashMap *frame = ptr_hashmap_create(interp->arena, 64);

  IDList host_allocs;
  list_init(&host_allocs);

  IRBasicBlock *entry_bb = list_entry(func->basic_blocks.next, IRBasicBlock, list_node);
  IRBasicBlock *prev_block = NULL;
  IRBasicBlock *current_block = entry_bb;

  IDList *inst_node;
  list_for_each(&entry_bb->instructions, inst_node)
  {
    IRInstruction *inst = list_entry(inst_node, IRInstruction, list_node);
    if (inst->opcode == IR_OP_ALLOCA)
    {
      IRType *ptr_type = inst->result.type;
      IRType *pointee_type = ptr_type->as.pointee_type;
      size_t alloc_size = get_type_size(pointee_type);

      void *host_ptr = malloc(alloc_size);

      HostAllocation *alloc_track = BUMP_ALLOC(interp->arena, HostAllocation);
      alloc_track->ptr = host_ptr;
      list_add_tail(&host_allocs, &alloc_track->list_node);

      RuntimeValue *rt_ptr_val = BUMP_ALLOC(interp->arena, RuntimeValue);
      rt_ptr_val->kind = RUNTIME_VAL_PTR;
      rt_ptr_val->as.val_ptr = host_ptr;

      set_value(frame, &inst->result, rt_ptr_val);
    }
  }

  size_t arg_idx = 0;
  IDList *arg_node;
  list_for_each(&func->arguments, arg_node)
  {
    assert(arg_idx < num_args && "Interpreter: Mismatched argument count");
    IRArgument *arg = list_entry(arg_node, IRArgument, list_node);
    set_value(frame, &arg->value, args[arg_idx]);
    arg_idx++;
  }

  while (current_block)
  {
    IRBasicBlock *next_block = NULL;
    IDList *inst_node;
    list_for_each(&current_block->instructions, inst_node)
    {
      IRInstruction *inst = list_entry(inst_node, IRInstruction, list_node);

      RuntimeValue *rt_lhs, *rt_rhs, *rt_val, *rt_ptr, *rt_res;
      IRValueNode *node_lhs;
      void *host_ptr;

      switch (inst->opcode)
      {

      case IR_OP_RET:
        node_lhs = get_operand_node(inst, 0);
        if (node_lhs)
        {
          rt_val = get_value(interp, frame, node_lhs);
          memcpy(result_out, rt_val, sizeof(RuntimeValue));
        }
        else
        {
          result_out->kind = RUNTIME_VAL_UNDEF;
        }
        current_block = NULL;
        goto cleanup;

      case IR_OP_BR:
        node_lhs = get_operand_node(inst, 0);
        next_block = container_of(node_lhs, IRBasicBlock, label_address);
        goto next_bb;

      case IR_OP_COND_BR:
        rt_val = get_value(interp, frame, get_operand_node(inst, 0));
        assert(rt_val->kind == RUNTIME_VAL_I1);

        if (rt_val->as.val_i1)
        {
          node_lhs = get_operand_node(inst, 1);
        }
        else
        {
          node_lhs = get_operand_node(inst, 2);
        }
        next_block = container_of(node_lhs, IRBasicBlock, label_address);
        goto next_bb;

      case IR_OP_ALLOCA:
        break;

      case IR_OP_STORE:
        rt_val = get_value(interp, frame, get_operand_node(inst, 0));
        rt_ptr = get_value(interp, frame, get_operand_node(inst, 1));
        assert(rt_ptr->kind == RUNTIME_VAL_PTR);
        host_ptr = rt_ptr->as.val_ptr;

        memcpy(host_ptr, &rt_val->as, get_type_size(get_operand_node(inst, 0)->type));
        break;

      case IR_OP_LOAD:
        rt_ptr = get_value(interp, frame, get_operand_node(inst, 0));
        assert(rt_ptr->kind == RUNTIME_VAL_PTR);
        host_ptr = rt_ptr->as.val_ptr;

        rt_res = BUMP_ALLOC(interp->arena, RuntimeValue);
        rt_res->kind = ir_to_runtime_kind(inst->result.type->kind);

        memcpy(&rt_res->as, host_ptr, get_type_size(inst->result.type));

        set_value(frame, &inst->result, rt_res);
        break;

      case IR_OP_PHI:
        rt_val = NULL;

        for (int i = 0; get_operand_node(inst, i) != NULL; i += 2)
        {
          IRValueNode *val_in_node = get_operand_node(inst, i);
          IRValueNode *bb_in_node = get_operand_node(inst, i + 1);
          IRBasicBlock *bb_in = container_of(bb_in_node, IRBasicBlock, label_address);

          if (bb_in == prev_block)
          {
            rt_val = get_value(interp, frame, val_in_node);
            break;
          }
        }
        assert(rt_val && "PHI node missing incoming value");
        set_value(frame, &inst->result, rt_val);
        break;
      case IR_OP_CALL:

        break;

      case IR_OP_ADD:
      case IR_OP_SUB:
        rt_lhs = get_value(interp, frame, get_operand_node(inst, 0));
        rt_rhs = get_value(interp, frame, get_operand_node(inst, 1));
        rt_res = BUMP_ALLOC(interp->arena, RuntimeValue);
        rt_res->kind = rt_lhs->kind;

        assert(rt_lhs->kind == RUNTIME_VAL_I32);
        if (inst->opcode == IR_OP_ADD)
        {
          rt_res->as.val_i32 = rt_lhs->as.val_i32 + rt_rhs->as.val_i32;
        }
        else
        {
          rt_res->as.val_i32 = rt_lhs->as.val_i32 - rt_rhs->as.val_i32;
        }
        set_value(frame, &inst->result, rt_res);
        break;

      case IR_OP_ICMP:
        rt_lhs = get_value(interp, frame, get_operand_node(inst, 0));
        rt_rhs = get_value(interp, frame, get_operand_node(inst, 1));
        rt_res = BUMP_ALLOC(interp->arena, RuntimeValue);
        rt_res->kind = RUNTIME_VAL_I1;

        assert(rt_lhs->kind == RUNTIME_VAL_I32);
        int32_t lhs_i32 = rt_lhs->as.val_i32;
        int32_t rhs_i32 = rt_rhs->as.val_i32;

        switch (inst->as.icmp.predicate)
        {
        case IR_ICMP_EQ:
          rt_res->as.val_i1 = (lhs_i32 == rhs_i32);
          break;
        case IR_ICMP_NE:
          rt_res->as.val_i1 = (lhs_i32 != rhs_i32);
          break;

        case IR_ICMP_SLT:
          rt_res->as.val_i1 = (lhs_i32 < rhs_i32);
          break;
        case IR_ICMP_SLE:
          rt_res->as.val_i1 = (lhs_i32 <= rhs_i32);
          break;
        case IR_ICMP_SGT:
          rt_res->as.val_i1 = (lhs_i32 > rhs_i32);
          break;
        case IR_ICMP_SGE:
          rt_res->as.val_i1 = (lhs_i32 >= rhs_i32);
          break;

        default:
          assert(false && "Unhandled ICMP predicate");
        }
        set_value(frame, &inst->result, rt_res);
        break;

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
        fprintf(stderr, "Interpreter Error: Unimplemented Integer/Bitwise Opcode.\n");

        assert(false && "Unimplemented Integer/Bitwise Opcode in interpreter");
        break;

      case IR_OP_FADD:
      case IR_OP_FSUB:
      case IR_OP_FMUL:
      case IR_OP_FDIV:
        fprintf(stderr, "Interpreter Error: Unimplemented Float Opcode.\n");
        assert(false && "Unimplemented Float Opcode in interpreter");
        break;

      case IR_OP_FCMP:
        fprintf(stderr, "Interpreter Error: Unimplemented Opcode: FCMP\n");
        assert(false && "Unimplemented IR_OP_FCMP in interpreter");
        break;

      case IR_OP_SWITCH:
        fprintf(stderr, "Interpreter Error: Unimplemented Opcode: SWITCH\n");
        assert(false && "Unimplemented IR_OP_SWITCH in interpreter");
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
        fprintf(stderr, "Interpreter Error: Unimplemented Cast Opcode.\n");
        assert(false && "Unimplemented Cast Opcode in interpreter");
        break;

      case IR_OP_GEP:

        assert(false && "GEP not implemented in interpreter yet");
        break;
      }
    }

  next_bb:
    prev_block = current_block;
    current_block = next_block;
  }

cleanup:

{
  IDList *iter, *temp;
  list_for_each_safe(&host_allocs, iter, temp)
  {
    HostAllocation *alloc = list_entry(iter, HostAllocation, list_node);
    free(alloc->ptr);
  }
}

  return true;
}
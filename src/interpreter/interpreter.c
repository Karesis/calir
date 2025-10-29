#include "interpreter/interpreter.h"

// 包含所有 IR 依赖项
#include "ir/basicblock.h"
#include "ir/constant.h"
#include "ir/function.h"
#include "ir/instruction.h"
#include "ir/type.h"
#include "ir/use.h"
#include "ir/value.h"

// 包含所有工具依赖项
#include "utils/bump.h"
#include "utils/hashmap.h"
#include "utils/id_list.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h> // for malloc, free, NULL
#include <string.h> // for memcpy

// -----------------------------------------------------------------
// 辅助宏和结构体
// -----------------------------------------------------------------

// 用于跟踪 'alloca' 分配的宿主内存
typedef struct HostAllocation
{
  void *ptr;
  IDList list_node;
} HostAllocation;

// -----------------------------------------------------------------
// 辅助函数
// -----------------------------------------------------------------

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
  // TODO: IR_TYPE_ARRAY, IR_TYPE_STRUCT
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

  // 1. 分配一个新的 RuntimeValue
  RuntimeValue *rt_val = BUMP_ALLOC_ZEROED(interp->arena, RuntimeValue);

  // 2. 根据常量类型填充
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

  // 3. 缓存并返回
  ptr_hashmap_put(frame, val_node, rt_val);
  return rt_val;
}

/**
 * @brief [核心] 获取一个 IRValueNode* 对应的 RuntimeValue*
 */
static RuntimeValue *
get_value(Interpreter *interp, PtrHashMap *frame, IRValueNode *val_node)
{
  // 1. 在执行帧 (HashMap) 中查找
  RuntimeValue *rt_val = ptr_hashmap_get(frame, val_node);
  if (rt_val)
  {
    return rt_val;
  }

  // 2. 如果找不到，检查是否是常量
  if (val_node->kind == IR_KIND_CONSTANT)
  {
    IRConstant *constant = container_of(val_node, IRConstant, value);
    return eval_constant(interp, frame, constant);
  }

  // 3. 既不是常量，也不在帧中 (例如：Gep, Function)
  // 暂时返回 NULL
  // assert(false && "Interpreter: Value not found in frame and is not a constant");
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

// -----------------------------------------------------------------
// 生命周期 API
// -----------------------------------------------------------------

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

// -----------------------------------------------------------------
// 核心执行 API
// -----------------------------------------------------------------

bool
interpreter_run_function(Interpreter *interp, IRFunction *func, RuntimeValue **args, size_t num_args,
                         RuntimeValue *result_out)
{
  assert(interp && func && result_out && "Invalid arguments for interpreter");

  // --- 1. 初始化 ---

  // 重置竞技场 (如果需要重用解释器)
  // bump_reset(interp->arena);

  // 创建执行帧 (IRValueNode* -> RuntimeValue*)
  PtrHashMap *frame = ptr_hashmap_create(interp->arena, 64);

  // 跟踪所有宿主内存分配 (alloca)
  IDList host_allocs;
  list_init(&host_allocs);

  IRBasicBlock *entry_bb = list_entry(func->basic_blocks.next, IRBasicBlock, list_node);
  IRBasicBlock *prev_block = NULL;
  IRBasicBlock *current_block = entry_bb;

  // --- 2. 处理 Allocas (在 entry 块) ---
  IDList *inst_node;
  list_for_each(&entry_bb->instructions, inst_node)
  {
    IRInstruction *inst = list_entry(inst_node, IRInstruction, list_node);
    if (inst->opcode == IR_OP_ALLOCA)
    {
      IRType *ptr_type = inst->result.type;
      IRType *pointee_type = ptr_type->as.pointee_type;
      size_t alloc_size = get_type_size(pointee_type);

      // [!!] alloca 分配 *宿主* 内存 (malloc)
      void *host_ptr = malloc(alloc_size);

      // 跟踪此分配，以便稍后 free
      HostAllocation *alloc_track = BUMP_ALLOC(interp->arena, HostAllocation);
      alloc_track->ptr = host_ptr;
      list_add_tail(&host_allocs, &alloc_track->list_node);

      // 创建一个 RuntimeValue (ptr)
      RuntimeValue *rt_ptr_val = BUMP_ALLOC(interp->arena, RuntimeValue);
      rt_ptr_val->kind = RUNTIME_VAL_PTR;
      rt_ptr_val->as.val_ptr = host_ptr;

      // 存入执行帧
      set_value(frame, &inst->result, rt_ptr_val);
    }
  }

  // --- 3. 设置参数 ---
  size_t arg_idx = 0;
  IDList *arg_node;
  list_for_each(&func->arguments, arg_node)
  {
    assert(arg_idx < num_args && "Interpreter: Mismatched argument count");
    IRArgument *arg = list_entry(arg_node, IRArgument, list_node);
    set_value(frame, &arg->value, args[arg_idx]);
    arg_idx++;
  }

  // --- 4. 执行循环 (CFG Walker) ---
  while (current_block)
  {
    IRBasicBlock *next_block = NULL;
    IDList *inst_node;
    list_for_each(&current_block->instructions, inst_node)
    {
      IRInstruction *inst = list_entry(inst_node, IRInstruction, list_node);

      // 辅助变量
      RuntimeValue *rt_lhs, *rt_rhs, *rt_val, *rt_ptr, *rt_res;
      IRValueNode *node_lhs, *node_rhs;
      void *host_ptr;

      switch (inst->opcode)
      {
      // --- 终结者指令 ---
      case IR_OP_RET:
        node_lhs = get_operand_node(inst, 0);
        if (node_lhs)
        { // ret <val>
          rt_val = get_value(interp, frame, node_lhs);
          memcpy(result_out, rt_val, sizeof(RuntimeValue));
        }
        else
        { // ret void
          result_out->kind = RUNTIME_VAL_UNDEF;
        }
        current_block = NULL; // 终止循环
        goto cleanup;         // [!!] 跳转到清理阶段

      case IR_OP_BR:
        node_lhs = get_operand_node(inst, 0); // 目标 BB
        next_block = container_of(node_lhs, IRBasicBlock, label_address);
        goto next_bb; // [!!] 跳转到块尾

      case IR_OP_COND_BR:
        rt_val = get_value(interp, frame, get_operand_node(inst, 0)); // cond (i1)
        assert(rt_val->kind == RUNTIME_VAL_I1);

        if (rt_val->as.val_i1)
        {                                       // true
          node_lhs = get_operand_node(inst, 1); // true_bb
        }
        else
        {                                       // false
          node_lhs = get_operand_node(inst, 2); // false_bb
        }
        next_block = container_of(node_lhs, IRBasicBlock, label_address);
        goto next_bb; // [!!] 跳转到块尾

      // --- 内存指令 ---
      case IR_OP_ALLOCA:
        break; // 已在循环前处理

      case IR_OP_STORE:
        rt_val = get_value(interp, frame, get_operand_node(inst, 0)); // value
        rt_ptr = get_value(interp, frame, get_operand_node(inst, 1)); // pointer
        assert(rt_ptr->kind == RUNTIME_VAL_PTR);
        host_ptr = rt_ptr->as.val_ptr;
        // [!!] 将 RuntimeValue 写入宿主内存
        memcpy(host_ptr, &rt_val->as, get_type_size(get_operand_node(inst, 0)->type));
        break;

      case IR_OP_LOAD:
        rt_ptr = get_value(interp, frame, get_operand_node(inst, 0)); // pointer
        assert(rt_ptr->kind == RUNTIME_VAL_PTR);
        host_ptr = rt_ptr->as.val_ptr;

        rt_res = BUMP_ALLOC(interp->arena, RuntimeValue);
        rt_res->kind = ir_to_runtime_kind(inst->result.type->kind);

        // [!!] 从宿主内存读取到 RuntimeValue
        memcpy(&rt_res->as, host_ptr, get_type_size(inst->result.type));

        set_value(frame, &inst->result, rt_res);
        break;

      // --- PHI 节点 ---
      case IR_OP_PHI:
        rt_val = NULL;
        // 遍历 [val, bb] 对
        for (int i = 0; get_operand_node(inst, i) != NULL; i += 2)
        {
          IRValueNode *val_in_node = get_operand_node(inst, i);
          IRValueNode *bb_in_node = get_operand_node(inst, i + 1);
          IRBasicBlock *bb_in = container_of(bb_in_node, IRBasicBlock, label_address);

          // [!!] 检查是否是从 'prev_block' 来的
          if (bb_in == prev_block)
          {
            rt_val = get_value(interp, frame, val_in_node);
            break;
          }
        }
        assert(rt_val && "PHI node missing incoming value");
        set_value(frame, &inst->result, rt_val);
        break;

      // --- 二元运算 ---
      case IR_OP_ADD:
      case IR_OP_SUB:
        rt_lhs = get_value(interp, frame, get_operand_node(inst, 0));
        rt_rhs = get_value(interp, frame, get_operand_node(inst, 1));
        rt_res = BUMP_ALLOC(interp->arena, RuntimeValue);
        rt_res->kind = rt_lhs->kind;

        // (暂时只处理 i32)
        assert(rt_lhs->kind == RUNTIME_VAL_I32);
        if (inst->opcode == IR_OP_ADD)
        {
          rt_res->as.val_i32 = rt_lhs->as.val_i32 + rt_rhs->as.val_i32;
        }
        else
        { // IR_OP_SUB
          rt_res->as.val_i32 = rt_lhs->as.val_i32 - rt_rhs->as.val_i32;
        }
        set_value(frame, &inst->result, rt_res);
        break;

      case IR_OP_ICMP:
        rt_lhs = get_value(interp, frame, get_operand_node(inst, 0));
        rt_rhs = get_value(interp, frame, get_operand_node(inst, 1));
        rt_res = BUMP_ALLOC(interp->arena, RuntimeValue);
        rt_res->kind = RUNTIME_VAL_I1; // ICMP 总是返回 i1

        // (暂时只处理 i32)
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
        // (假定为有符号比较，你的谓词区分了有符号/无符号)
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
        // TODO: 添加 UGT, UGE, ULT, ULE
        default:
          assert(false && "Unhandled ICMP predicate");
        }
        set_value(frame, &inst->result, rt_res);
        break;

      case IR_OP_GEP:
        // TODO: GEP 涉及复杂的指针运算
        assert(false && "GEP not implemented in interpreter yet");
        break;
      }
    } // 结束指令循环

  next_bb:
    prev_block = current_block;
    current_block = next_block;
  } // 结束 CFG 循环

cleanup:
  // --- 5. 清理宿主内存 (alloca) ---
  {
    IDList *iter, *temp;
    list_for_each_safe(&host_allocs, iter, temp)
    {
      HostAllocation *alloc = list_entry(iter, HostAllocation, list_node);
      free(alloc->ptr); // [!!] 释放 malloc 的内存
    }
  }

  return true; // 成功执行 (遇到了 'ret')
}
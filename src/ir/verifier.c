#include "ir/verifier.h"

// 包含所有 IR 组件的完整定义
#include "ir/basicblock.h"
#include "ir/context.h"
#include "ir/function.h"
#include "ir/global.h"
#include "ir/instruction.h"
#include "ir/module.h"
#include "ir/type.h"
#include "ir/use.h"
#include "ir/value.h"
#include "utils/id_list.h"

#include <stdio.h> // for fprintf, stderr

/*
 * =================================================================
 * --- 辅助宏 ---
 * =================================================================
 */

// 辅助结构体，用于在打印错误时携带上下文信息
typedef struct
{
  IRFunction *current_function;
  IRBasicBlock *current_block;
  bool has_error;
} VerifierContext;

/**
 * @brief 报告一个验证错误
 *
 * @param vctx Verifier 上下文 (用于跟踪状态)
 * @param obj 导致错误的对象 (例如 IRValueNode*, IRBasicBlock*)
 * @param ... 格式化错误信息 (printf 风格)
 */
#define VERIFY_ERROR(vctx, obj, ...)                                                                                   \
  do                                                                                                                   \
  {                                                                                                                    \
    if (!(vctx)->has_error)                                                                                            \
    { /* 只打印第一个错误，防止信息泛滥 */                                                                             \
      fprintf(stderr, "\n--- [CALIR VERIFIER ERROR] ---\n");                                                           \
      if ((vctx)->current_function)                                                                                    \
      {                                                                                                                \
        fprintf(stderr, "In Function: %s\n", (vctx)->current_function->entry_address.name);                            \
      }                                                                                                                \
      if ((vctx)->current_block)                                                                                       \
      {                                                                                                                \
        fprintf(stderr, "In Block:    %s\n", (vctx)->current_block->label_address.name);                               \
      }                                                                                                                \
      fprintf(stderr, "Error:       ");                                                                                \
      fprintf(stderr, __VA_ARGS__);                                                                                    \
      fprintf(stderr, "\n");                                                                                           \
      if (obj)                                                                                                         \
      {                                                                                                                \
        /* 尝试打印导致错误的对象 */                                                                                   \
        if (((IRValueNode *)(obj))->kind == IR_KIND_INSTRUCTION)                                                       \
        {                                                                                                              \
          fprintf(stderr, "Object:      ");                                                                            \
          ir_instruction_dump(container_of(obj, IRInstruction, result), stderr);                                       \
        }                                                                                                              \
        else if (((IRValueNode *)(obj))->kind == IR_KIND_BASIC_BLOCK)                                                  \
        {                                                                                                              \
          /* (ir_basic_block_dump 可能会打印过多信息，先只打印 Value) */                                               \
          fprintf(stderr, "Object:      ");                                                                            \
          ir_value_dump((IRValueNode *)obj, stderr);                                                                   \
          fprintf(stderr, "\n");                                                                                       \
        }                                                                                                              \
        else                                                                                                           \
        {                                                                                                              \
          fprintf(stderr, "Object:      ");                                                                            \
          ir_value_dump((IRValueNode *)obj, stderr);                                                                   \
          fprintf(stderr, "\n");                                                                                       \
        }                                                                                                              \
      }                                                                                                                \
      fprintf(stderr, "---------------------------------\n");                                                          \
      (vctx)->has_error = true;                                                                                        \
    }                                                                                                                  \
    return false; /* 导致验证函数返回 false */                                                                         \
  } while (0)

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
 * --- 内部辅助函数 (基于你的 id_list.h) ---
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
  return NULL; // 索引越界
}

/*
 * =================================================================
 * --- 内部验证函数 ---
 * =================================================================
 */

// 前向声明
static bool verify_basic_block(VerifierContext *vctx, IRBasicBlock *bb);
static bool verify_instruction(VerifierContext *vctx, IRInstruction *inst);

/**
 * @brief 验证单条指令
 */
static bool
verify_instruction(VerifierContext *vctx, IRInstruction *inst)
{
  IRValueNode *value = &inst->result;
  IRBasicBlock *bb = inst->parent;
  VERIFY_ASSERT(bb != NULL, vctx, value, "Instruction has no parent BasicBlock.");

  IRFunction *func = bb->parent;
  VERIFY_ASSERT(func != NULL, vctx, value, "Instruction's parent block has no parent Function.");

  IRContext *ctx = func->parent->context;
  VERIFY_ASSERT(ctx != NULL, vctx, value, "Failed to get Context from Function.");

  // 获取指令的结果类型
  IRType *result_type = value->type;
  VERIFY_ASSERT(result_type != NULL, vctx, value, "Instruction result has NULL type.");

  // --- 1. 检查操作数 (Operands) 和 Def-Use 链 ---
  IDList *iter_node;
  list_for_each(&inst->operands, iter_node)
  {
    IRUse *use = list_entry(iter_node, IRUse, user_node);
    VERIFY_ASSERT(use->user == inst, vctx, value, "Inconsistent Use-Def chain: use->user points to wrong instruction.");
    VERIFY_ASSERT(use->value != NULL, vctx, value, "Instruction has a NULL operand (use->value is NULL).");
    VERIFY_ASSERT(use->value->type != NULL, vctx, use->value, "Instruction operand has NULL type.");

    // TODO: 添加 SSA 支配性检查 (高级)
  }
  int op_count = get_operand_count(inst);

  // --- 2. 根据 Opcode 检查特定规则 ---
  switch (inst->opcode)
  {
  // --- 终结者指令 ---
  case IR_OP_RET: {
    IRType *func_ret_type = func->return_type;
    if (func_ret_type->kind == IR_TYPE_VOID)
    {
      VERIFY_ASSERT(op_count == 0, vctx, value, "'ret' in a void function must have 0 operands.");
    }
    else
    {
      VERIFY_ASSERT(op_count == 1, vctx, value, "'ret' in a non-void function must have 1 operand.");
      IRValueNode *ret_val = get_operand(inst, 0);
      VERIFY_ASSERT(ret_val->type == func_ret_type, vctx, value, "'ret' operand type mismatch.");
    }
    break;
  }
  case IR_OP_BR: {
    VERIFY_ASSERT(op_count == 1, vctx, value, "'br' instruction must have exactly 1 operand (target_bb).");
    IRValueNode *target = get_operand(inst, 0);
    VERIFY_ASSERT(target->type->kind == IR_TYPE_LABEL, vctx, target, "'br' operand must be a label type.");
    break;
  }
  case IR_OP_COND_BR: {
    VERIFY_ASSERT(op_count == 3, vctx, value, "'cond_br' must have exactly 3 operands (cond, true_bb, false_bb).");
    IRValueNode *cond = get_operand(inst, 0);
    IRValueNode *true_bb = get_operand(inst, 1);
    IRValueNode *false_bb = get_operand(inst, 2);

    VERIFY_ASSERT(cond->type->kind == IR_TYPE_I1, vctx, cond, "'cond_br' condition must be of type i1.");
    VERIFY_ASSERT(true_bb->type->kind == IR_TYPE_LABEL, vctx, true_bb, "'cond_br' true target must be a label type.");
    VERIFY_ASSERT(false_bb->type->kind == IR_TYPE_LABEL, vctx, false_bb,
                  "'cond_br' false target must be a label type.");
    break;
  }

  // --- 二元运算 ---
  case IR_OP_ADD:
  case IR_OP_SUB: {
    VERIFY_ASSERT(op_count == 2, vctx, value, "Binary op must have 2 operands.");
    IRValueNode *lhs = get_operand(inst, 0);
    IRValueNode *rhs = get_operand(inst, 1);

    VERIFY_ASSERT(lhs->type == rhs->type, vctx, value, "Binary op operands must have the same type.");
    bool is_valid_type = (lhs->type->kind >= IR_TYPE_I8 && lhs->type->kind <= IR_TYPE_F64);
    VERIFY_ASSERT(is_valid_type, vctx, lhs, "Binary op operands must be integer or float type.");
    VERIFY_ASSERT(result_type == lhs->type, vctx, value, "Binary op result type must match operand type.");
    break;
  }
  case IR_OP_ICMP: {
    VERIFY_ASSERT(op_count == 2, vctx, value, "'icmp' must have 2 operands.");
    IRValueNode *lhs = get_operand(inst, 0);
    IRValueNode *rhs = get_operand(inst, 1);

    VERIFY_ASSERT(lhs->type == rhs->type, vctx, value, "'icmp' operands must have the same type.");
    bool is_valid_type =
      (lhs->type->kind >= IR_TYPE_I1 && lhs->type->kind <= IR_TYPE_I64) || (lhs->type->kind == IR_TYPE_PTR);
    VERIFY_ASSERT(is_valid_type, vctx, lhs, "'icmp' operands must be integer or pointer type.");
    VERIFY_ASSERT(result_type->kind == IR_TYPE_I1, vctx, value, "'icmp' result type must be i1.");
    break;
  }

  // --- 内存操作 ---
  case IR_OP_ALLOCA: {
    VERIFY_ASSERT(op_count == 0, vctx, value, "'alloca' instruction should have no operands.");
    VERIFY_ASSERT(result_type->kind == IR_TYPE_PTR, vctx, value, "'alloca' result must be a pointer type.");

    // 检查： 'alloca' 必须在函数入口块
    IDList *entry_bb_node = func->basic_blocks.next;
    IRBasicBlock *entry_block = list_entry(entry_bb_node, IRBasicBlock, list_node);
    VERIFY_ASSERT(bb == entry_block, vctx, value, "'alloca' instruction must be in the function's entry block.");
    break;
  }
  case IR_OP_LOAD: {
    VERIFY_ASSERT(op_count == 1, vctx, value, "'load' must have exactly one operand (the pointer).");
    IRValueNode *ptr = get_operand(inst, 0);
    VERIFY_ASSERT(ptr->type->kind == IR_TYPE_PTR, vctx, ptr, "'load' operand must be a pointer type.");

    IRType *pointee_type = ptr->type->as.pointee_type;
    VERIFY_ASSERT(result_type == pointee_type, vctx, value,
                  "'load' result type must match the pointer's pointee type.");
    break;
  }
  case IR_OP_STORE: {
    VERIFY_ASSERT(op_count == 2, vctx, value, "'store' must have exactly 2 operands (value, pointer).");
    IRValueNode *val_to_store = get_operand(inst, 0);
    IRValueNode *ptr = get_operand(inst, 1);

    VERIFY_ASSERT(ptr->type->kind == IR_TYPE_PTR, vctx, ptr, "'store' second operand must be a pointer type.");
    IRType *pointee_type = ptr->type->as.pointee_type;
    VERIFY_ASSERT(val_to_store->type == pointee_type, vctx, value,
                  "'store' value type must match the pointer's pointee type.");
    VERIFY_ASSERT(result_type->kind == IR_TYPE_VOID, vctx, value, "'store' instruction result type must be void.");
    break;
  }
  case IR_OP_PHI: {
    VERIFY_ASSERT(op_count > 0, vctx, value, "'phi' node cannot be empty.");
    VERIFY_ASSERT(op_count % 2 == 0, vctx, value, "'phi' node must have an even number of operands ([val, bb] pairs).");

    for (int i = 0; i < op_count; i += 2)
    {
      IRValueNode *val = get_operand(inst, i);
      IRValueNode *incoming_bb_val = get_operand(inst, i + 1);
      VERIFY_ASSERT(val->type == result_type, vctx, val, "PHI incoming value type mismatch.");
      VERIFY_ASSERT(incoming_bb_val->type->kind == IR_TYPE_LABEL, vctx, incoming_bb_val,
                    "PHI incoming block must be a label type.");

      // TODO: (高级) 检查 incoming_bb_val 是否是 vctx->current_block 的有效前驱
    }
    break;
  }
  case IR_OP_GEP: {
    VERIFY_ASSERT(op_count > 1, vctx, value, "'getelementptr' must have at least 2 operands (base_ptr, index).");
    IRValueNode *base_ptr = get_operand(inst, 0);
    VERIFY_ASSERT(base_ptr->type->kind == IR_TYPE_PTR, vctx, base_ptr, "GEP base operand must be a pointer.");
    VERIFY_ASSERT(inst->as.gep.source_type == base_ptr->type->as.pointee_type, vctx, value,
                  "GEP source_type mismatch with base_ptr pointee type.");
    VERIFY_ASSERT(result_type->kind == IR_TYPE_PTR, vctx, value, "GEP result must be a pointer type.");

    // TODO: (高级) 遍历索引，根据 source_type 验证 GEP 是否合法
    break;
  }

  default:
    VERIFY_ERROR(vctx, value, "Unknown instruction opcode: %d", inst->opcode);
  }

  return true;
}

/**
 * @brief 验证单个基本块
 */
static bool
verify_basic_block(VerifierContext *vctx, IRBasicBlock *bb)
{
  VERIFY_ASSERT(bb != NULL, vctx, NULL, "BasicBlock is NULL.");
  VERIFY_ASSERT(bb->parent != NULL, vctx, &bb->label_address, "BasicBlock has no parent Function.");

  vctx->current_block = bb; // 设置上下文

  if (list_empty(&bb->instructions))
  {
    VERIFY_ERROR(vctx, &bb->label_address, "BasicBlock cannot be empty. Must have at least one terminator.");
  }

  // --- 1. 检查终结者指令 ---
  IDList *last_inst_node = bb->instructions.prev;
  IRInstruction *last_inst = list_entry(last_inst_node, IRInstruction, list_node);

  bool is_terminator =
    (last_inst->opcode == IR_OP_RET || last_inst->opcode == IR_OP_BR || last_inst->opcode == IR_OP_COND_BR);
  VERIFY_ASSERT(is_terminator, vctx, &last_inst->result,
                "BasicBlock must end with a terminator instruction (ret, br, cond_br).");

  // --- 2. 遍历所有指令 ---
  bool processing_phis = true;
  IDList *iter_node;
  list_for_each(&bb->instructions, iter_node)
  {
    IRInstruction *inst = list_entry(iter_node, IRInstruction, list_node);

    // 检查父指针
    VERIFY_ASSERT(inst->parent == bb, vctx, &inst->result, "Instruction's parent pointer is incorrect.");

    // 检查 PHI 节点是否都在开头
    if (inst->opcode == IR_OP_PHI)
    {
      VERIFY_ASSERT(processing_phis, vctx, &inst->result, "PHI instruction found after non-PHI instruction.");
    }
    else
    {
      processing_phis = false;
    }

    // 检查非末尾指令
    if (inst != last_inst)
    {
      bool inst_is_terminator =
        (inst->opcode == IR_OP_RET || inst->opcode == IR_OP_BR || inst->opcode == IR_OP_COND_BR);
      VERIFY_ASSERT(!inst_is_terminator, vctx, &inst->result,
                    "Terminator instruction found in the middle of a BasicBlock.");
    }

    // --- 3. 验证指令自身 ---
    if (!verify_instruction(vctx, inst))
    {
      return false; // 错误已打印
    }
  }

  vctx->current_block = NULL; // 离开 BB 上下文
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
  VerifierContext vctx = {0};
  VERIFY_ASSERT(func != NULL, &vctx, NULL, "Function is NULL.");
  VERIFY_ASSERT(func->parent != NULL, &vctx, &func->entry_address, "Function has no parent Module.");
  VERIFY_ASSERT(func->return_type != NULL, &vctx, &func->entry_address, "Function has NULL return type.");

  vctx.current_function = func; // 设置上下文

  if (list_empty(&func->basic_blocks))
  {
    // 这是一个声明 (e.g., 'declare i32 @puts(ptr)')
    // 规则: 声明的参数 *必须没有* 名字
    IDList *arg_it;
    list_for_each(&func->arguments, arg_it)
    {
      IRArgument *arg = list_entry(arg_it, IRArgument, list_node);
      VERIFY_ASSERT(arg->value.name == NULL, &vctx, &arg->value,
                    "Argument in a function *declaration* cannot have a name.");
    }
    return !vctx.has_error; // 声明验证通过
  }

  // --- 1. 验证函数参数 ---
  IDList *arg_it;
  list_for_each(&func->arguments, arg_it)
  {
    IRArgument *arg = list_entry(arg_it, IRArgument, list_node);
    VERIFY_ASSERT(arg->parent == func, &vctx, &arg->value, "Argument's parent pointer is incorrect.");
    VERIFY_ASSERT(arg->value.type != NULL, &vctx, &arg->value, "Argument has NULL type.");
    VERIFY_ASSERT(arg->value.type->kind != IR_TYPE_VOID, &vctx, &arg->value,
                  "Function argument cannot have void type.");
    // 规则: 定义的参数 *必须有* 名字 (否则无法被引用)
    VERIFY_ASSERT(arg->value.name != NULL, &vctx, &arg->value, "Argument in a function *definition* must have a name.");
  }

  // --- 2. 验证所有基本块 ---
  IDList *bb_it;
  list_for_each(&func->basic_blocks, bb_it)
  {
    IRBasicBlock *bb = list_entry(bb_it, IRBasicBlock, list_node);
    VERIFY_ASSERT(bb->parent == func, &vctx, &bb->label_address, "BasicBlock's parent pointer is incorrect.");

    if (!verify_basic_block(&vctx, bb))
    {
      return false; // 错误已打印
    }
  }

  return !vctx.has_error;
}

bool
ir_verify_module(IRModule *mod)
{
  VerifierContext vctx = {0};
  VERIFY_ASSERT(mod != NULL, &vctx, NULL, "Module is NULL.");
  VERIFY_ASSERT(mod->context != NULL, &vctx, NULL, "Module has no Context.");

  // --- 验证所有全局变量 ---
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
      VERIFY_ASSERT(global->initializer->kind == IR_KIND_CONSTANT, &vctx, global->initializer,
                    "Global initializer must be a constant.");
      VERIFY_ASSERT(global->initializer->type == global->allocated_type, &vctx, global->initializer,
                    "Global initializer type mismatch allocated_type.");
    }
  }

  // --- 验证所有函数 ---
  IDList *func_it;
  list_for_each(&mod->functions, func_it)
  {
    IRFunction *func = list_entry(func_it, IRFunction, list_node);
    VERIFY_ASSERT(func->parent == mod, &vctx, &func->entry_address, "Function's parent pointer is incorrect.");

    if (!ir_verify_function(func))
    {
      // 错误已在 ir_verify_function 中打印，这里只需返回 false
      return false;
    }
  }

  return !vctx.has_error;
}
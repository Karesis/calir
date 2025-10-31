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
#include "ir/type.h"
#include "ir/use.h"
#include "ir/value.h"
#include "utils/bump.h"
#include "utils/id_list.h"

#include <stdint.h> // for uint64_t
#include <stdio.h>  // for fprintf, stderr

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
  DominatorTree *dom_tree; // 缓存支配树
  Bump analysis_arena;     // 用于所有函数级分析的竞技场
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

  if (term->opcode == IR_OP_BR)
  {
    // 'br'只有一个目标
    return (get_operand(term, 0) == target_val);
  }
  else if (term->opcode == IR_OP_COND_BR)
  {
    // 'cond_br'有两个目标
    return (get_operand(term, 1) == target_val || get_operand(term, 2) == target_val);
  }

  // 不是一个 'br' (例如, 'ret'), 所以它不是任何块的前驱
  return false;
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

  // 遍历 PHI 的操作数对, 只检查 [..., bb] 部分
  for (int i = 1; i < count; i += 2)
  {
    if (get_operand(phi_inst, i) == pred_val)
    {
      return true;
    }
  }
  return false;
}

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

    // --- SSA 支配性检查 (Intra-block) ---

    // 规则: 一个 'def' (操作数) 必须支配它的 'use' (当前指令)

    // 1. 跳过非指令的 'def' (常量, 参数, 全局变量等, 它们总是支配)
    if (use->value->kind != IR_KIND_INSTRUCTION)
    {
      continue;
    }

    // 2. 跳过 PHI 节点 (它们有特殊的 SSA 规则, 在 'case IR_OP_PHI' 中单独检查)
    // PHI 的 'use' 概念上发生在 *前驱块* 的末尾, 而不是当前块。
    if (inst->opcode == IR_OP_PHI)
    {
      continue;
    }

    // --- 核心检查 ---
    IRInstruction *def_inst = container_of(use->value, IRInstruction, result);
    IRBasicBlock *def_bb = def_inst->parent;
    IRBasicBlock *use_bb = inst->parent; // inst 是 'use'

    if (def_bb == use_bb)
    {
      // **Intra-block check (同块内检查)**
      // 'def' 和 'use' 在同一个基本块中。
      // 'def' 必须出现在 'use' *之前*。

      bool def_found_before_use = false;
      IDList *prev_node = inst->list_node.prev; // 从 'use' 指令的前一条开始

      // 反向遍历, 直到撞到链表头
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
      // **Inter-block check (跨块检查)**
      // 规则: def_bb 必须 *支配* (dominate) use_bb。

      // (def_bb 在这里是一个 IRBasicBlock*)
      bool dominates = dom_tree_dominates(vctx->dom_tree, def_bb, use_bb);

      VERIFY_ASSERT(dominates, vctx, &inst->result,
                    "SSA VIOLATION: Definition in block '%s' does not dominate use in block '%s'.",
                    def_bb->label_address.name, use_bb->label_address.name);
    }
    // --- 检查结束 ---
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
    VERIFY_ASSERT(target->kind == IR_KIND_BASIC_BLOCK, vctx, target, "'br' operand must be a Basic Block.");
    break;
  }
  case IR_OP_COND_BR: {
    VERIFY_ASSERT(op_count == 3, vctx, value, "'cond_br' must have exactly 3 operands (cond, true_bb, false_bb).");
    IRValueNode *cond = get_operand(inst, 0);
    IRValueNode *true_bb = get_operand(inst, 1);
    IRValueNode *false_bb = get_operand(inst, 2);

    VERIFY_ASSERT(cond->type->kind == IR_TYPE_I1, vctx, cond, "'cond_br' condition must be of type i1.");
    VERIFY_ASSERT(true_bb->kind == IR_KIND_BASIC_BLOCK, vctx, true_bb, "'cond_br' true target must be a Basic Block.");
    VERIFY_ASSERT(false_bb->kind == IR_KIND_BASIC_BLOCK, vctx, false_bb,
                  "'cond_br' false target must be a Basic Block.");
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

    int phi_entry_count = op_count / 2;
    IRFunction *func = vctx->current_function;
    IRBasicBlock *current_bb = vctx->current_block;

    // --- 1. 检查类型 和 检查重复条目 ---
    for (int i = 0; i < op_count; i += 2)
    {
      IRValueNode *val = get_operand(inst, i);
      IRValueNode *incoming_bb_val = get_operand(inst, i + 1);

      // 检查类型
      VERIFY_ASSERT(val->type == result_type, vctx, val, "PHI incoming value type mismatch.");
      VERIFY_ASSERT(incoming_bb_val->kind == IR_KIND_BASIC_BLOCK, vctx, incoming_bb_val,
                    "PHI incoming block must be a Basic Block.");

      // 检查重复: 确保同一个 BB 没有被列出两次
      for (int j = i + 2; j < op_count; j += 2)
      {
        IRValueNode *other_bb_val = get_operand(inst, j);
        VERIFY_ASSERT(incoming_bb_val != other_bb_val, vctx, &inst->result,
                      "PHI node contains duplicate entry for the same incoming block.");
      }
    }

    // --- 2. 检查完整性 (所有前驱都被覆盖) ---
    // 遍历函数中的 *所有* 基本块，找到 'current_bb' 的实际前驱
    int actual_pred_count = 0;
    IDList *all_blocks_iter;
    list_for_each(&func->basic_blocks, all_blocks_iter)
    {
      IRBasicBlock *potential_pred = list_entry(all_blocks_iter, IRBasicBlock, list_node);

      // (一个块不能是它自己的前驱... 除非是循环, 终结者检查会处理这个)
      if (potential_pred == current_bb)
        continue;
      // (跳过空块, 尽管它们不应该存在)
      if (list_empty(&potential_pred->instructions))
        continue;

      // 获取这个块的终结者指令
      IRInstruction *term = list_entry(potential_pred->instructions.prev, IRInstruction, list_node);

      // 检查这个终结者是否跳转到 'current_bb'
      if (is_terminator_predecessor(term, current_bb))
      {
        actual_pred_count++;

        // 这个 `potential_pred` *必须* 在 PHI 节点中被列出
        bool found_in_phi = find_in_phi(inst, potential_pred);
        VERIFY_ASSERT(found_in_phi, vctx, &inst->result, "PHI node is missing an entry for predecessor block '%s'.",
                      potential_pred->label_address.name);
      }
    }

    // --- 3. 检查合理性 (没有多余的条目) ---
    // 如果 1 (无重复) 和 2 (都找到) 通过了,
    // 我们只需要检查条目总数是否匹配。
    // 如果 PHI 条目比实际前驱多, 意味着它包含了一个 *不是* 前驱的无效条目。
    VERIFY_ASSERT(phi_entry_count == actual_pred_count, vctx, &inst->result,
                  "PHI node has incorrect number of entries. Found %d, but expected %d (actual predecessors).",
                  phi_entry_count, actual_pred_count);

    break;
  }
  case IR_OP_GEP: {
    // --- GEP 索引验证开始 ---

    IRType *current_type = inst->as.gep.source_type;

    // 遍历所有 *索引* (操作数 1 到 op_count-1)
    for (int i = 1; i < op_count; i++)
    {
      IRValueNode *index_val = get_operand(inst, i);

      // 1. 所有索引都必须是整数
      bool is_int_type = (index_val->type->kind >= IR_TYPE_I8 && index_val->type->kind <= IR_TYPE_I64);
      VERIFY_ASSERT(is_int_type, vctx, index_val, "GEP index must be an integer type (i8-i64).");

      // 2. 第一个索引 (i == 1) 索引指针，不"剥离"类型。
      // 我们在循环外已经验证了 source_type，所以这里 continue。
      if (i == 1)
      {
        continue;
      }

      // 3. 索引 2 及以后 (i >= 2) 开始剥离类型。
      //    current_type 必须是一个聚合类型。
      switch (current_type->kind)
      {
      case IR_TYPE_ARRAY:
        // 索引数组：类型变为元素类型
        current_type = current_type->as.array.element_type;
        break;

      case IR_TYPE_STRUCT: {
        // 索引结构体：

        // 3a. 索引必须是常量
        VERIFY_ASSERT(index_val->kind == IR_KIND_CONSTANT, vctx, index_val,
                      "GEP index into a struct *must* be a constant integer.");

        // 3b. 提取常量值 (这需要 #include "ir/constant.h")
        IRConstant *k = (IRConstant *)index_val;
        VERIFY_ASSERT(k->const_kind == CONST_KIND_INT, vctx, index_val,
                      "GEP struct index is a constant, but not an *integer* constant.");

        uint64_t member_idx = (uint64_t)k->data.int_val;

        // 3c. 索引必须在界内
        VERIFY_ASSERT(member_idx < current_type->as.aggregate.member_count, vctx, index_val,
                      "GEP struct index is out of bounds.");

        // 3d. 更新类型为成员类型
        current_type = current_type->as.aggregate.member_types[member_idx];
        break;
      }
      default:
        // 错误：试图索引一个非聚合类型 (e.g., i32, ptr)
        VERIFY_ERROR(vctx, &inst->result, "GEP is trying to index into a non-aggregate type (e.g., i32, ptr).");
      }
    }

    // 4. 最终检查：
    // 我们模拟计算出的 GEP 结果类型（`ptr to current_type`）
    // 必须与指令上存储的结果类型（`result_type`）完全一致。
    IRType *expected_result_type = ir_type_get_ptr(ctx, current_type);
    VERIFY_ASSERT(result_type == expected_result_type, vctx, &inst->result,
                  "GEP result type is incorrect. Builder calculation does not match verifier calculation.");

    // --- GEP 验证结束 ---
    break;
  }

  case IR_OP_CALL: {
    // 1. 验证至少有 Callee
    VERIFY_ASSERT(op_count >= 1, vctx, value, "'call' must have at least 1 operand (the callee).");

    // 2. 验证 Callee 类型
    IRValueNode *callee_val = get_operand(inst, 0);
    VERIFY_ASSERT(callee_val->type->kind == IR_TYPE_PTR, vctx, callee_val, "'call' callee must be a pointer type.");

    IRType *callee_pointee_type = callee_val->type->as.pointee_type;
    VERIFY_ASSERT(callee_pointee_type->kind == IR_TYPE_FUNCTION, vctx, callee_val,
                  "'call' callee must be a *pointer to a function type*.");

    // 3. 提取函数类型 (我们已验证它是 IR_TYPE_FUNCTION)
    IRType *func_type = callee_pointee_type;

    // 4. 验证结果类型
    VERIFY_ASSERT(result_type == func_type->as.function.return_type, vctx, value,
                  "'call' result type does not match callee's function type return type.");

    // 5. 验证参数数量
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
                    "'call' argument count mismatch. Expected %zu, but got %zu.", expected_arg_count,
                    provided_arg_count);
    }

    // 6. 验证 *固定* 参数的类型 (逐个对比)
    for (size_t i = 0; i < expected_arg_count; i++)
    {
      IRValueNode *provided_arg = get_operand(inst, i + 1); // (i+1 跳过 callee)
      IRType *expected_type = func_type->as.function.param_types[i];

      VERIFY_ASSERT(provided_arg->type == expected_type, vctx, provided_arg,
                    "'call' argument %zu type mismatch. Expected type %p, got %p.", i, expected_type,
                    provided_arg->type);
    }

    // (可变参数 '...' 部分的类型是未知的, Verifier 无法检查)
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

  // 初始化分析
  bump_init(&vctx.analysis_arena); // 初始化竞技场
  FunctionCFG *cfg = NULL;
  DominatorTree *doms = NULL;

  if (list_empty(&func->basic_blocks))
  {
    // 这是一个声明 (e.g., 'declare i32 @puts(ptr)')
    // 规则: 声明的参数 *必须没有* 名字
    IDList *arg_it;
    list_for_each(&func->arguments, arg_it)
    {
      IRArgument *arg = list_entry(arg_it, IRArgument, list_node);
      // 我们仍然需要验证参数类型
      VERIFY_ASSERT(arg->value.type != NULL, &vctx, &arg->value, "Argument has NULL type.");
      VERIFY_ASSERT(arg->value.type->kind != IR_TYPE_VOID, &vctx, &arg->value,
                    "Function argument cannot have void type.");
    }
    bump_destroy(&vctx.analysis_arena); // 清理空分析的竞技场
    return !vctx.has_error;             // 声明验证通过
  }

  // --- 0. 运行分析遍 ---
  // 仅对函数定义运行

  // 1. 构建 CFG
  cfg = cfg_build(func, &vctx.analysis_arena);

  // 2. 构建支配树
  doms = dom_tree_build(cfg, &vctx.analysis_arena);

  vctx.dom_tree = doms; // <-- 存入上下文

  // (如果 doms == NULL，说明 cfg 为空或入口块为空，
  //  后续的 dom_tree_dominates 查询会安全地处理 NULL)

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
      // 错误退出时清理
      if (doms)
        dom_tree_destroy(doms); // (空操作，但保持对称)
      if (cfg)
        cfg_destroy(cfg);                 // (释放 cfg->arena)
      bump_destroy(&vctx.analysis_arena); // 释放 vctx.analysis_arena
      return false;                       // 错误已打印
    }
  }

  // 成功退出时清理
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
      // 初始值必须是 "全局" 的
      bool is_valid_initializer = (global->initializer->kind == IR_KIND_CONSTANT) || // e.g., 10
                                  (global->initializer->kind == IR_KIND_FUNCTION) || // e.g., @my_func
                                  (global->initializer->kind == IR_KIND_GLOBAL);     // e.g., @other_global
      VERIFY_ASSERT(is_valid_initializer, &vctx, global->initializer,
                    "Global initializer must be a constant, function, or another global.");
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
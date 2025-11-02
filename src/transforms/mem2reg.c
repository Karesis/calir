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


#include "transforms/mem2reg.h"

// 所有的依赖项
#include "analysis/cfg.h"
#include "analysis/dom_frontier.h"
#include "analysis/dom_tree.h"
#include "ir/builder.h"
#include "ir/context.h"
#include "ir/function.h"
#include "ir/instruction.h"
#include "ir/type.h"
#include "ir/use.h"
#include "ir/value.h"
#include "utils/bitset.h"
#include "utils/bump.h"
#include "utils/hashmap.h"
#include "utils/id_list.h"

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h> // for NULL

// -----------------------------------------------------------------
// 辅助结构体 (Helper Structs)
// -----------------------------------------------------------------

/**
 * @brief 存储 mem2reg pass 期间所需的所有上下文。
 */
typedef struct
{
  IRFunction *func;
  DominatorTree *dt;
  DominanceFrontier *df;
  IRContext *ctx;
  Bump *arena; // 用于分配所有临时数据 (BitSets, Stacks, ...)
  IRBuilder *builder;

  // 用于 Pass 1 & 2
  size_t num_blocks; // 缓存 dt->cfg->num_nodes
} Mem2RegContext;

/**
 * @brief 存储单个 alloca 的所有分析信息。
 */
typedef struct
{
  IRInstruction *alloca_inst; // alloca 指令本身
  IRType *allocated_type;     // 被分配的类型 (e.g., i32)

  /** 存储此 alloca 的块的集合 (Bitset) */
  Bitset *def_blocks;
  /** 需要插入 PHI 节点的块的集合 (Bitset) */
  Bitset *phi_blocks;

  IDList list_node; // 用于加入 promotable_allocas 链表
} AllocaInfo;

/**
 * @brief 重命名栈上的一个节点。
 * (这是一个简单的侵入式链表，用作栈)
 */
typedef struct RenameStackNode
{
  IRValueNode *value;
  struct RenameStackNode *prev; // 指向栈中的下一个值
} RenameStackNode;

/**
 * @brief 重命名阶段的完整状态。
 */
typedef struct
{
  Bump *arena; // 用于分配 RenameStackNode
  /** 映射: AllocaInst* -> RenameStackNode* (指向栈顶) */
  PtrHashMap *alloca_to_stack_top;
} RenameState;

// -----------------------------------------------------------------
// Pass 1: 查找 Promotable Allocas
// -----------------------------------------------------------------

/**
 * @brief 检查 alloca 是否只被 load/store 使用。
 *
 * 如果 alloca 指针 "逃逸" (例如, 被 GEP, 被 store 到别处, 或作为参数传递),
 * 它就不能被提升。
 */
static bool
is_promotable(IRInstruction *alloca_inst)
{
  assert(alloca_inst->opcode == IR_OP_ALLOCA);
  IRValueNode *alloca_val = &alloca_inst->result;

  // 遍历 alloca 的所有 uses
  IDList *use_node;
  list_for_each(&alloca_val->uses, use_node)
  {
    IRUse *use = list_entry(use_node, IRUse, value_node);
    IRInstruction *user_inst = use->user;

    if (user_inst->opcode == IR_OP_LOAD)
    {
      // OK: load ptr %alloca
      continue;
    }

    if (user_inst->opcode == IR_OP_STORE)
    {
      // 检查 alloca 是 *指针* 操作数 (operand 1)
      // 而不是 *值* 操作数 (operand 0)
      IRUse *val_use = list_entry(user_inst->operands.next, IRUse, user_node);
      IRUse *ptr_use = list_entry(val_use->user_node.next, IRUse, user_node);

      if (ptr_use->value == alloca_val)
      {
        // OK: store i32 %val, ptr %alloca
        continue;
      }
    }

    // 任何其他使用 (GEP, call, bitcast, or store %alloca, ptr %other)
    // 都被视为“逃逸”，不可提升。
    return false;
  }

  return true;
}

/**
 * @brief 收集所有可提升的 alloca，并找到它们的 'def_blocks' (store)
 */
static void
find_promotable_allocas(Mem2RegContext *ctx, IDList *promotable_allocas)
{
  // 1. 获取入口块
  if (list_empty(&ctx->func->basic_blocks))
  {
    return; // 没有块
  }
  IRBasicBlock *entry_bb = list_entry(ctx->func->basic_blocks.next, IRBasicBlock, list_node);

  // 2. 遍历入口块的指令
  IDList *inst_node;
  list_for_each(&entry_bb->instructions, inst_node)
  {
    IRInstruction *inst = list_entry(inst_node, IRInstruction, list_node);
    if (inst->opcode != IR_OP_ALLOCA)
    {
      continue;
    }

    // 3. 检查 alloca 的类型 (不能提升 [10 x i32] 或 %struct)
    IRType *ptr_type = inst->result.type;
    if (ptr_type->kind != IR_TYPE_PTR)
    {
      continue; // (这不应该发生)
    }
    IRType *pointee_type = ptr_type->as.pointee_type;
    if (pointee_type->kind == IR_TYPE_ARRAY || pointee_type->kind == IR_TYPE_STRUCT)
    {
      continue; // 不提升聚合类型
    }

    // 4. 检查 alloca 是否 "逃逸"
    if (!is_promotable(inst))
    {
      continue;
    }

    // --- 此 alloca 可提升 ---

    AllocaInfo *info = BUMP_ALLOC_ZEROED(ctx->arena, AllocaInfo);
    info->alloca_inst = inst;
    info->allocated_type = pointee_type;
    info->def_blocks = bitset_create(ctx->num_blocks, ctx->arena);
    info->phi_blocks = bitset_create(ctx->num_blocks, ctx->arena);

    // 5. 找到所有的 'def_blocks' (store)
    IDList *use_node;
    list_for_each(&inst->result.uses, use_node)
    {
      IRUse *use = list_entry(use_node, IRUse, value_node);
      if (use->user->opcode == IR_OP_STORE)
      {
        // (我们已经在 is_promotable 中确认了这是 store *to* alloca)
        CFGNode *node = cfg_get_node(ctx->dt->cfg, use->user->parent);
        bitset_set(info->def_blocks, node->id);
      }
    }

    list_add_tail(promotable_allocas, &info->list_node);
  }
}

// -----------------------------------------------------------------
// Pass 2: 计算 PHI 插入点 (Iterated Dominance Frontier)
// -----------------------------------------------------------------

static void
compute_phi_placement(Mem2RegContext *ctx, AllocaInfo *info)
{
  // [!!] 这是 Iterated Dominance Frontier (IDF) 算法

  // 1. 我们需要一个实际的 worklist 栈 (Bitset 不适合做 worklist)
  size_t *worklist_stack = BUMP_ALLOC_SLICE(ctx->arena, size_t, ctx->num_blocks);
  size_t worklist_count = 0;

  // 2. 初始化 worklist 和 phi_blocks
  for (size_t b_id = 0; b_id < ctx->num_blocks; b_id++)
  {
    if (bitset_test(info->def_blocks, b_id))
    {
      worklist_stack[worklist_count++] = b_id;
      // phi_blocks = IDF(Def Blocks), not Def Blocks U IDF(Def Blocks)
      // bitset_set(info->phi_blocks, b_id); // 保证 PHI 节点在所有 def 块中
    }
  }

  // 3. 迭代，直到 worklist 为空
  while (worklist_count > 0)
  {
    size_t b_id = worklist_stack[--worklist_count];
    CFGNode *b_node = &ctx->dt->cfg->nodes[b_id];

    // 4. 获取 b_id 的支配边界 DF(b_id)
    Bitset *df_b = ir_analysis_dom_frontier_get(ctx->df, b_node->block);
    if (!df_b)
      continue;

    // 5. for each block 'd' in DF(b)
    for (size_t d_id = 0; d_id < ctx->num_blocks; d_id++)
    {
      if (bitset_test(df_b, d_id))
      {
        // 6. if 'd' 还没有 PHI
        if (!bitset_test(info->phi_blocks, d_id))
        {
          // 7. 添加 PHI，并将 'd' 加入 worklist
          bitset_set(info->phi_blocks, d_id);
          worklist_stack[worklist_count++] = d_id;
        }
      }
    }
  }
}

// -----------------------------------------------------------------
// Pass 3: 插入 PHI 节点
// -----------------------------------------------------------------

/**
 * @brief 在所有标记的块中插入空的 PHI 节点
 * @return 一个 Map (AllocaInst* -> Map<IRBasicBlock*, PHIInst*>)
 * (我们用一个 PtrHashMap<PHIInst*, AllocaInst*> 来反向映射)
 */
static PtrHashMap *
insert_phi_nodes(Mem2RegContext *ctx, IDList *promotable_allocas)
{
  // 我们需要一个 PtrHashMap<PHIInst*, AllocaInst*>
  PtrHashMap *phi_to_alloca_map = ptr_hashmap_create(ctx->arena, 16);

  IDList *info_node;
  list_for_each(promotable_allocas, info_node)
  {
    AllocaInfo *info = list_entry(info_node, AllocaInfo, list_node);

    for (size_t b_id = 0; b_id < ctx->num_blocks; b_id++)
    {
      if (bitset_test(info->phi_blocks, b_id))
      {
        IRBasicBlock *bb = ctx->dt->cfg->nodes[b_id].block;

        // 1. 设置 builder 在 *块的开头* 插入
        ir_builder_set_insertion_point(ctx->builder, bb);
        // (builder->create_phi 知道要插入到开头)

        // 2. 创建 PHI
        IRValueNode *phi_val = ir_builder_create_phi(ctx->builder, info->allocated_type, NULL);
        IRInstruction *phi_inst = container_of(phi_val, IRInstruction, result);

        // 3. 存储映射: phi -> alloca
        ptr_hashmap_put(phi_to_alloca_map, phi_inst, info->alloca_inst);
      }
    }
  }
  return phi_to_alloca_map;
}

// -----------------------------------------------------------------
// Pass 4: 重命名 (核心算法)
// -----------------------------------------------------------------

// --- RenameState 栈操作 ---

static void
push_stack(RenameState *state, IRInstruction *alloca, IRValueNode *value)
{
  RenameStackNode *old_top = ptr_hashmap_get(state->alloca_to_stack_top, alloca);
  RenameStackNode *new_top = BUMP_ALLOC(state->arena, RenameStackNode);
  new_top->value = value;
  new_top->prev = old_top;
  ptr_hashmap_put(state->alloca_to_stack_top, alloca, new_top);
}

static void
pop_stack(RenameState *state, IRInstruction *alloca)
{
  RenameStackNode *top = ptr_hashmap_get(state->alloca_to_stack_top, alloca);
  assert(top && "Popping an empty stack!");
  ptr_hashmap_put(state->alloca_to_stack_top, alloca, top->prev);
  // (BUMP_ALLOC: 不需要 free 'top')
}

static IRValueNode *
get_stack_top(RenameState *state, IRInstruction *alloca)
{
  RenameStackNode *top = ptr_hashmap_get(state->alloca_to_stack_top, alloca);
  assert(top && "Getting from an empty stack!");
  return top->value;
}

/**
 * @brief 递归重命名
 * @param node 当前的支配树节点
 */
static void
rename_recursive(Mem2RegContext *ctx, DomTreeNode *node, RenameState *state, PtrHashMap *phi_to_alloca_map)
{
  IRBasicBlock *bb = node->cfg_node->block;

  // (使用栈上的小数组，因为大多数块不会定义超过 64 个值)
  IRInstruction *pushed_allocas[64];
  size_t pushed_count = 0;

  // (同样，待删除的指令列表)
  IRInstruction *to_delete[256];
  size_t delete_count = 0;

  // --- 1. 遍历当前块的指令 ---
  IDList *inst_node, *tmp_node;
  list_for_each_safe(&bb->instructions, inst_node, tmp_node)
  {
    IRInstruction *inst = list_entry(inst_node, IRInstruction, list_node);

    if (inst->opcode == IR_OP_PHI)
    {
      // 是我们插入的 PHI 吗?
      IRInstruction *alloca = ptr_hashmap_get(phi_to_alloca_map, inst);
      if (alloca)
      {
        // 是的, 将此 PHI 的 *结果* 压入栈
        push_stack(state, alloca, &inst->result);
        pushed_allocas[pushed_count++] = alloca;
        assert(pushed_count < 64 && "Rename stack overflow");
      }
    }
    else if (inst->opcode == IR_OP_LOAD)
    {
      // 是 load promotable alloca 吗?
      IRUse *ptr_use = list_entry(inst->operands.next, IRUse, user_node);
      IRValueNode *ptr_val = ptr_use->value;

      if (ptr_val->kind == IR_KIND_INSTRUCTION)
      {
        IRInstruction *alloca = container_of(ptr_val, IRInstruction, result);
        if (ptr_hashmap_contains(state->alloca_to_stack_top, alloca))
        {
          // 是的! 用栈顶的值替换 'load' 的所有 use
          IRValueNode *live_val = get_stack_top(state, alloca);
          ir_value_replace_all_uses_with(&inst->result, live_val);
          // 标记此 'load' 稍后删除
          to_delete[delete_count++] = inst;
          assert(delete_count < 256 && "Delete list overflow");
        }
      }
    }
    else if (inst->opcode == IR_OP_STORE)
    {
      // 是 store *to* promotable alloca 吗?
      IRUse *val_use = list_entry(inst->operands.next, IRUse, user_node);
      IRUse *ptr_use = list_entry(val_use->user_node.next, IRUse, user_node);
      IRValueNode *ptr_val = ptr_use->value;

      if (ptr_val->kind == IR_KIND_INSTRUCTION)
      {
        IRInstruction *alloca = container_of(ptr_val, IRInstruction, result);
        if (ptr_hashmap_contains(state->alloca_to_stack_top, alloca))
        {
          // 是的! 将 *被 store 的值* 压入栈
          IRValueNode *stored_val = val_use->value;
          push_stack(state, alloca, stored_val);
          pushed_allocas[pushed_count++] = alloca;
          assert(pushed_count < 64 && "Rename stack overflow");
          // 标记此 'store' 稍后删除
          to_delete[delete_count++] = inst;
          assert(delete_count < 256 && "Delete list overflow");
        }
      }
    }
  }

  // --- 2. 填充 CFG 后继块中的 PHI 节点 ---
  IDList *succ_edge_node;
  list_for_each(&node->cfg_node->successors, succ_edge_node)
  {
    CFGEdge *edge = list_entry(succ_edge_node, CFGEdge, list_node);
    IRBasicBlock *succ_bb = edge->node->block;

    // 遍历 *后继块* 的 PHI 节点
    IDList *succ_inst_node;
    list_for_each(&succ_bb->instructions, succ_inst_node)
    {
      IRInstruction *succ_phi = list_entry(succ_inst_node, IRInstruction, list_node);
      if (succ_phi->opcode != IR_OP_PHI)
      {
        break; // PHI 必须在块的开头
      }

      // 是我们插入的 PHI 吗?
      IRInstruction *alloca = ptr_hashmap_get(phi_to_alloca_map, succ_phi);
      if (alloca)
      {
        // 是的! 获取 *当前块* 的活动值
        IRValueNode *outgoing_val = get_stack_top(state, alloca);
        // 添加 [value, current_bb] 到 PHI
        ir_phi_add_incoming(&succ_phi->result, outgoing_val, bb);
      }
    }
  }

  // --- 3. 递归到支配树的子节点 ---
  IDList *child_node;
  list_for_each(&node->children, child_node)
  {
    DomTreeChild *child = list_entry(child_node, DomTreeChild, list_node);
    rename_recursive(ctx, child->node, state, phi_to_alloca_map);
  }

  // --- 4. 清理: 弹出此块 push 的所有值 ---
  for (size_t i = 0; i < pushed_count; i++)
  {
    pop_stack(state, pushed_allocas[i]);
  }

  // --- 5. 清理: 删除死去的 load/store 指令 ---
  for (size_t i = 0; i < delete_count; i++)
  {
    IRInstruction *inst = to_delete[i];
    list_del(&inst->list_node);             // 1. 从 BB 链表移除
    ir_instruction_erase_from_parent(inst); // 2. 解除所有 Use 边并释放
  }
}

// -----------------------------------------------------------------
// 公共 API (Public API)
// -----------------------------------------------------------------

bool
ir_transform_mem2reg_run(IRFunction *func, DominatorTree *dt, DominanceFrontier *df)
{
  // --- 0. 初始化上下文 ---
  // 我们将所有临时数据（AllocaInfo, BitSets, RenameStacks）
  // 分配在 IR Arena 上。这没问题，因为这个 pass 完成后，
  // verifier 通常会运行，或者我们会进入下一个阶段。
  // 如果 IR Arena 在函数之间重用，这可能需要一个专用的 Arena。
  // (假设 ir_arena 是有效的)
  IRContext *ctx = func->parent->context;
  Mem2RegContext m2r_ctx = {
    .func = func,
    .dt = dt,
    .df = df,
    .ctx = ctx,
    .arena = &ctx->ir_arena, // [!!] 假设所有临时数据随 IR 一起分配
    .builder = ir_builder_create(ctx),
    .num_blocks = dt->cfg->num_nodes,
  };

  IDList promotable_allocas; // 这是一个链表头
  list_init(&promotable_allocas);

  // --- 1. 查找可提升的 Allocas ---
  find_promotable_allocas(&m2r_ctx, &promotable_allocas);

  if (list_empty(&promotable_allocas))
  {
    ir_builder_destroy(m2r_ctx.builder);
    return false; // 没有可提升的 alloca, 无事可做
  }

  // --- 2. 计算 PHI 插入点 (IDF) ---
  IDList *info_node;
  list_for_each(&promotable_allocas, info_node)
  {
    AllocaInfo *info = list_entry(info_node, AllocaInfo, list_node);
    compute_phi_placement(&m2r_ctx, info);
  }

  // --- 3. 插入 PHI 节点 ---
  PtrHashMap *phi_to_alloca_map = insert_phi_nodes(&m2r_ctx, &promotable_allocas);

  // --- 4. 重命名 ---
  // 4.1 初始化重命名栈
  RenameState rename_state = {
    .arena = m2r_ctx.arena,
    .alloca_to_stack_top = ptr_hashmap_create(m2r_ctx.arena, 16),
  };

  // 4.2 用 'undef' 填充所有栈的底部
  list_for_each(&promotable_allocas, info_node)
  {
    AllocaInfo *info = list_entry(info_node, AllocaInfo, list_node);
    IRValueNode *undef = ir_constant_get_undef(ctx, info->allocated_type);
    // (push_stack 会创建 stack-top map)
    push_stack(&rename_state, info->alloca_inst, undef);
  }

  // 4.3 从支配树的根开始递归
  rename_recursive(&m2r_ctx, dt->root, &rename_state, phi_to_alloca_map);

  // --- 5. 清理: 删除 alloca 指令 ---
  IDList *tmp_node;
  list_for_each_safe(&promotable_allocas, info_node, tmp_node)
  {
    AllocaInfo *info = list_entry(info_node, AllocaInfo, list_node);
    IRInstruction *alloca_inst = info->alloca_inst;

    list_del(&alloca_inst->list_node);             // 1. 从 BB 链表移除
    ir_instruction_erase_from_parent(alloca_inst); // 2. 销毁
                                                   // info 结构体本身在 arena 上，无需 free
  }

  // --- 6. 销毁 Builder ---
  ir_builder_destroy(m2r_ctx.builder);

  return true; // IR 已被修改
}
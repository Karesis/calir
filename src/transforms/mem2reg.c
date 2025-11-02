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
#include <stdlib.h>





/**
 * @brief 存储 mem2reg pass 期间所需的所有上下文。
 */
typedef struct
{
  IRFunction *func;
  DominatorTree *dt;
  DominanceFrontier *df;
  IRContext *ctx;
  Bump *arena;
  IRBuilder *builder;


  size_t num_blocks;
} Mem2RegContext;

/**
 * @brief 存储单个 alloca 的所有分析信息。
 */
typedef struct
{
  IRInstruction *alloca_inst;
  IRType *allocated_type;

  /** 存储此 alloca 的块的集合 (Bitset) */
  Bitset *def_blocks;
  /** 需要插入 PHI 节点的块的集合 (Bitset) */
  Bitset *phi_blocks;

  IDList list_node;
} AllocaInfo;

/**
 * @brief 重命名栈上的一个节点。
 * (这是一个简单的侵入式链表，用作栈)
 */
typedef struct RenameStackNode
{
  IRValueNode *value;
  struct RenameStackNode *prev;
} RenameStackNode;

/**
 * @brief 重命名阶段的完整状态。
 */
typedef struct
{
  Bump *arena;
  /** 映射: AllocaInst* -> RenameStackNode* (指向栈顶) */
  PtrHashMap *alloca_to_stack_top;
} RenameState;





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


  IDList *use_node;
  list_for_each(&alloca_val->uses, use_node)
  {
    IRUse *use = list_entry(use_node, IRUse, value_node);
    IRInstruction *user_inst = use->user;

    if (user_inst->opcode == IR_OP_LOAD)
    {

      continue;
    }

    if (user_inst->opcode == IR_OP_STORE)
    {


      IRUse *val_use = list_entry(user_inst->operands.next, IRUse, user_node);
      IRUse *ptr_use = list_entry(val_use->user_node.next, IRUse, user_node);

      if (ptr_use->value == alloca_val)
      {

        continue;
      }
    }



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

  if (list_empty(&ctx->func->basic_blocks))
  {
    return;
  }
  IRBasicBlock *entry_bb = list_entry(ctx->func->basic_blocks.next, IRBasicBlock, list_node);


  IDList *inst_node;
  list_for_each(&entry_bb->instructions, inst_node)
  {
    IRInstruction *inst = list_entry(inst_node, IRInstruction, list_node);
    if (inst->opcode != IR_OP_ALLOCA)
    {
      continue;
    }


    IRType *ptr_type = inst->result.type;
    if (ptr_type->kind != IR_TYPE_PTR)
    {
      continue;
    }
    IRType *pointee_type = ptr_type->as.pointee_type;
    if (pointee_type->kind == IR_TYPE_ARRAY || pointee_type->kind == IR_TYPE_STRUCT)
    {
      continue;
    }


    if (!is_promotable(inst))
    {
      continue;
    }



    AllocaInfo *info = BUMP_ALLOC_ZEROED(ctx->arena, AllocaInfo);
    info->alloca_inst = inst;
    info->allocated_type = pointee_type;
    info->def_blocks = bitset_create(ctx->num_blocks, ctx->arena);
    info->phi_blocks = bitset_create(ctx->num_blocks, ctx->arena);


    IDList *use_node;
    list_for_each(&inst->result.uses, use_node)
    {
      IRUse *use = list_entry(use_node, IRUse, value_node);
      if (use->user->opcode == IR_OP_STORE)
      {

        CFGNode *node = cfg_get_node(ctx->dt->cfg, use->user->parent);
        bitset_set(info->def_blocks, node->id);
      }
    }

    list_add_tail(promotable_allocas, &info->list_node);
  }
}





static void
compute_phi_placement(Mem2RegContext *ctx, AllocaInfo *info)
{



  size_t *worklist_stack = BUMP_ALLOC_SLICE(ctx->arena, size_t, ctx->num_blocks);
  size_t worklist_count = 0;


  for (size_t b_id = 0; b_id < ctx->num_blocks; b_id++)
  {
    if (bitset_test(info->def_blocks, b_id))
    {
      worklist_stack[worklist_count++] = b_id;


    }
  }


  while (worklist_count > 0)
  {
    size_t b_id = worklist_stack[--worklist_count];
    CFGNode *b_node = &ctx->dt->cfg->nodes[b_id];


    Bitset *df_b = ir_analysis_dom_frontier_get(ctx->df, b_node->block);
    if (!df_b)
      continue;


    for (size_t d_id = 0; d_id < ctx->num_blocks; d_id++)
    {
      if (bitset_test(df_b, d_id))
      {

        if (!bitset_test(info->phi_blocks, d_id))
        {

          bitset_set(info->phi_blocks, d_id);
          worklist_stack[worklist_count++] = d_id;
        }
      }
    }
  }
}





/**
 * @brief 在所有标记的块中插入空的 PHI 节点
 * @return 一个 Map (AllocaInst* -> Map<IRBasicBlock*, PHIInst*>)
 * (我们用一个 PtrHashMap<PHIInst*, AllocaInst*> 来反向映射)
 */
static PtrHashMap *
insert_phi_nodes(Mem2RegContext *ctx, IDList *promotable_allocas)
{

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


        ir_builder_set_insertion_point(ctx->builder, bb);



        IRValueNode *phi_val = ir_builder_create_phi(ctx->builder, info->allocated_type, NULL);
        IRInstruction *phi_inst = container_of(phi_val, IRInstruction, result);


        ptr_hashmap_put(phi_to_alloca_map, phi_inst, info->alloca_inst);
      }
    }
  }
  return phi_to_alloca_map;
}







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


  IRInstruction *pushed_allocas[64];
  size_t pushed_count = 0;


  IRInstruction *to_delete[256];
  size_t delete_count = 0;


  IDList *inst_node, *tmp_node;
  list_for_each_safe(&bb->instructions, inst_node, tmp_node)
  {
    IRInstruction *inst = list_entry(inst_node, IRInstruction, list_node);

    if (inst->opcode == IR_OP_PHI)
    {

      IRInstruction *alloca = ptr_hashmap_get(phi_to_alloca_map, inst);
      if (alloca)
      {

        push_stack(state, alloca, &inst->result);
        pushed_allocas[pushed_count++] = alloca;
        assert(pushed_count < 64 && "Rename stack overflow");
      }
    }
    else if (inst->opcode == IR_OP_LOAD)
    {

      IRUse *ptr_use = list_entry(inst->operands.next, IRUse, user_node);
      IRValueNode *ptr_val = ptr_use->value;

      if (ptr_val->kind == IR_KIND_INSTRUCTION)
      {
        IRInstruction *alloca = container_of(ptr_val, IRInstruction, result);
        if (ptr_hashmap_contains(state->alloca_to_stack_top, alloca))
        {

          IRValueNode *live_val = get_stack_top(state, alloca);
          ir_value_replace_all_uses_with(&inst->result, live_val);

          to_delete[delete_count++] = inst;
          assert(delete_count < 256 && "Delete list overflow");
        }
      }
    }
    else if (inst->opcode == IR_OP_STORE)
    {

      IRUse *val_use = list_entry(inst->operands.next, IRUse, user_node);
      IRUse *ptr_use = list_entry(val_use->user_node.next, IRUse, user_node);
      IRValueNode *ptr_val = ptr_use->value;

      if (ptr_val->kind == IR_KIND_INSTRUCTION)
      {
        IRInstruction *alloca = container_of(ptr_val, IRInstruction, result);
        if (ptr_hashmap_contains(state->alloca_to_stack_top, alloca))
        {

          IRValueNode *stored_val = val_use->value;
          push_stack(state, alloca, stored_val);
          pushed_allocas[pushed_count++] = alloca;
          assert(pushed_count < 64 && "Rename stack overflow");

          to_delete[delete_count++] = inst;
          assert(delete_count < 256 && "Delete list overflow");
        }
      }
    }
  }


  IDList *succ_edge_node;
  list_for_each(&node->cfg_node->successors, succ_edge_node)
  {
    CFGEdge *edge = list_entry(succ_edge_node, CFGEdge, list_node);
    IRBasicBlock *succ_bb = edge->node->block;


    IDList *succ_inst_node;
    list_for_each(&succ_bb->instructions, succ_inst_node)
    {
      IRInstruction *succ_phi = list_entry(succ_inst_node, IRInstruction, list_node);
      if (succ_phi->opcode != IR_OP_PHI)
      {
        break;
      }


      IRInstruction *alloca = ptr_hashmap_get(phi_to_alloca_map, succ_phi);
      if (alloca)
      {

        IRValueNode *outgoing_val = get_stack_top(state, alloca);

        ir_phi_add_incoming(&succ_phi->result, outgoing_val, bb);
      }
    }
  }


  IDList *child_node;
  list_for_each(&node->children, child_node)
  {
    DomTreeChild *child = list_entry(child_node, DomTreeChild, list_node);
    rename_recursive(ctx, child->node, state, phi_to_alloca_map);
  }


  for (size_t i = 0; i < pushed_count; i++)
  {
    pop_stack(state, pushed_allocas[i]);
  }


  for (size_t i = 0; i < delete_count; i++)
  {
    IRInstruction *inst = to_delete[i];
    list_del(&inst->list_node);
    ir_instruction_erase_from_parent(inst);
  }
}





bool
ir_transform_mem2reg_run(IRFunction *func, DominatorTree *dt, DominanceFrontier *df)
{






  IRContext *ctx = func->parent->context;
  Mem2RegContext m2r_ctx = {
    .func = func,
    .dt = dt,
    .df = df,
    .ctx = ctx,
    .arena = &ctx->ir_arena,
    .builder = ir_builder_create(ctx),
    .num_blocks = dt->cfg->num_nodes,
  };

  IDList promotable_allocas;
  list_init(&promotable_allocas);


  find_promotable_allocas(&m2r_ctx, &promotable_allocas);

  if (list_empty(&promotable_allocas))
  {
    ir_builder_destroy(m2r_ctx.builder);
    return false;
  }


  IDList *info_node;
  list_for_each(&promotable_allocas, info_node)
  {
    AllocaInfo *info = list_entry(info_node, AllocaInfo, list_node);
    compute_phi_placement(&m2r_ctx, info);
  }


  PtrHashMap *phi_to_alloca_map = insert_phi_nodes(&m2r_ctx, &promotable_allocas);



  RenameState rename_state = {
    .arena = m2r_ctx.arena,
    .alloca_to_stack_top = ptr_hashmap_create(m2r_ctx.arena, 16),
  };


  list_for_each(&promotable_allocas, info_node)
  {
    AllocaInfo *info = list_entry(info_node, AllocaInfo, list_node);
    IRValueNode *undef = ir_constant_get_undef(ctx, info->allocated_type);

    push_stack(&rename_state, info->alloca_inst, undef);
  }


  rename_recursive(&m2r_ctx, dt->root, &rename_state, phi_to_alloca_map);


  IDList *tmp_node;
  list_for_each_safe(&promotable_allocas, info_node, tmp_node)
  {
    AllocaInfo *info = list_entry(info_node, AllocaInfo, list_node);
    IRInstruction *alloca_inst = info->alloca_inst;

    list_del(&alloca_inst->list_node);
    ir_instruction_erase_from_parent(alloca_inst);

  }


  ir_builder_destroy(m2r_ctx.builder);

  return true;
}
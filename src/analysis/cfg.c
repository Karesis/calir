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

#include "analysis/cfg.h"
#include "ir/instruction.h"
#include "ir/use.h"
#include "ir/value.h"
#include "utils/id_list.h"

/**
 * @brief 获取指令的第 N 个操作数 (ValueNode)
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

/**
 * @brief [内部] 向 CFG 添加一条边 (使用你的 bump.h API)
 */
static void
cfg_add_edge(FunctionCFG *cfg, CFGNode *from, CFGNode *to)
{
  if (!from || !to)
    return;

  CFGEdge *succ_edge = BUMP_ALLOC(&cfg->arena, CFGEdge);
  succ_edge->node = to;
  list_add_tail(&from->successors, &succ_edge->list_node);

  CFGEdge *pred_edge = BUMP_ALLOC(&cfg->arena, CFGEdge);
  pred_edge->node = from;
  list_add_tail(&to->predecessors, &pred_edge->list_node);
}

FunctionCFG *
cfg_build(IRFunction *func, Bump *arena)
{

  FunctionCFG *cfg = BUMP_ALLOC_ZEROED(arena, FunctionCFG);
  cfg->func = func;

  bump_init(&cfg->arena);

  cfg->num_nodes = 0;
  IDList *bb_it;
  list_for_each(&func->basic_blocks, bb_it)
  {
    cfg->num_nodes++;
  }

  cfg->block_to_node_map = ptr_hashmap_create(&cfg->arena, cfg->num_nodes);

  if (cfg->num_nodes == 0)
  {
    cfg->nodes = NULL;
    cfg->entry_node = NULL;

    return cfg;
  }

  cfg->nodes = BUMP_ALLOC_SLICE(&cfg->arena, CFGNode, cfg->num_nodes);

  int current_id = 0;
  list_for_each(&func->basic_blocks, bb_it)
  {
    IRBasicBlock *bb = list_entry(bb_it, IRBasicBlock, list_node);
    CFGNode *node = &cfg->nodes[current_id];

    node->block = bb;
    node->id = current_id;

    list_init(&node->successors);
    list_init(&node->predecessors);

    if (current_id == 0)
    {
      cfg->entry_node = node;
    }

    ptr_hashmap_put(cfg->block_to_node_map, bb, node);

    current_id++;
  }

  for (int i = 0; i < cfg->num_nodes; i++)
  {
    CFGNode *current_node = &cfg->nodes[i];
    IRBasicBlock *bb = current_node->block;

    if (list_empty(&bb->instructions))
    {
      continue;
    }

    IRInstruction *term = list_entry(bb->instructions.prev, IRInstruction, list_node);

    switch (term->opcode)
    {
    case IR_OP_BR: {
      IRValueNode *target_val = get_operand(term, 0);

      IRBasicBlock *target_bb = (IRBasicBlock *)target_val;
      CFGNode *target_node = cfg_get_node(cfg, target_bb);

      cfg_add_edge(cfg, current_node, target_node);
      break;
    }

    case IR_OP_COND_BR: {

      IRValueNode *true_val = get_operand(term, 1);
      IRBasicBlock *true_bb = (IRBasicBlock *)true_val;
      CFGNode *true_node = cfg_get_node(cfg, true_bb);

      cfg_add_edge(cfg, current_node, true_node);

      IRValueNode *false_val = get_operand(term, 2);
      IRBasicBlock *false_bb = (IRBasicBlock *)false_val;
      CFGNode *false_node = cfg_get_node(cfg, false_bb);

      if (true_node != false_node)
      {
        cfg_add_edge(cfg, current_node, false_node);
      }
      break;
    }

    case IR_OP_RET:
    default: {

      break;
    }
    }
  }

  return cfg;
}

void
cfg_destroy(FunctionCFG *cfg)
{
  if (!cfg)
    return;

  bump_destroy(&cfg->arena);
}
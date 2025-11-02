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


#include "analysis/dom_frontier.h"
#include "analysis/cfg.h"
#include "utils/bitset.h"
#include "utils/bump.h"
#include "utils/id_list.h"

/**
 * @brief 递归辅助函数，用于计算支配边界
 *
 * 这是一个自底向上 (post-order) 的支配树遍历。
 * 算法 (来自 "Engineering a Compiler", Fig. 10.12):
 *
 * for each node n (in bottom-up dominator tree order):
 * DF(n) = {}
 *
 * // 1. DF_local: 贡献来自 CFG 后继
 * for each successor y of n:
 * if idom(y) != n:
 * DF(n) = DF(n) U {y}
 *
 * // 2. DF_up: 贡献来自支配树的子节点
 * for each child c of n (in dominator tree):
 * // (递归调用已在此之前完成)
 * for each w in DF(c):
 * if idom(w) != n:
 * DF(n) = DF(n) U {w}
 */
static void
compute_df_recursive(DomTreeNode *n, DominanceFrontier *df, Bitset *temp_set)
{
  DominatorTree *dt = df->dom_tree;
  size_t num_blocks = df->num_blocks;



  Bitset *df_n = df->frontiers[n->cfg_node->id];




  IDList *succ_list = &n->cfg_node->successors;
  IDList *edge_node;
  list_for_each(succ_list, edge_node)
  {
    CFGEdge *edge = list_entry(edge_node, CFGEdge, list_node);
    CFGNode *y_cfg = edge->node;



    DomTreeNode *y_dom_node = dt->nodes[y_cfg->id];
    DomTreeNode *idom_y = y_dom_node->idom;

    if (idom_y != n)
    {

      bitset_set(df_n, y_cfg->id);
    }
  }



  IDList *child_list = &n->children;
  IDList *child_list_node;
  list_for_each(child_list, child_list_node)
  {
    DomTreeChild *child = list_entry(child_list_node, DomTreeChild, list_node);
    DomTreeNode *c = child->node;


    compute_df_recursive(c, df, temp_set);


    Bitset *df_c = df->frontiers[c->cfg_node->id];




    bitset_clear_all(temp_set);



    for (size_t w_id = 0; w_id < num_blocks; w_id++)
    {
      if (bitset_test(df_c, w_id))
      {

        DomTreeNode *w_dom_node = dt->nodes[w_id];
        DomTreeNode *idom_w = w_dom_node->idom;

        if (idom_w != n)
        {

          bitset_set(temp_set, w_id);
        }
      }
    }



    bitset_union(df_n, df_n, temp_set);
  }
}

/**
 * @brief 计算给定函数的支配边界。
 */
DominanceFrontier *
ir_analysis_dom_frontier_compute(DominatorTree *dt, Bump *arena)
{
  FunctionCFG *cfg = dt->cfg;
  size_t num_blocks = cfg->num_nodes;


  DominanceFrontier *df = BUMP_ALLOC_ZEROED(arena, DominanceFrontier);
  df->dom_tree = dt;
  df->num_blocks = num_blocks;
  df->arena = arena;



  df->frontiers = BUMP_ALLOC_SLICE_ZEROED(arena, Bitset *, num_blocks);


  for (size_t i = 0; i < num_blocks; i++)
  {

    df->frontiers[i] = bitset_create(num_blocks, arena);
  }


  Bitset *temp_set = bitset_create(num_blocks, arena);


  compute_df_recursive(dt->root, df, temp_set);



  return df;
}

/**
 * @brief 释放 DominanceFrontier 结构占用的内存。
 * (通常为空，因为内存由 arena 管理)
 */
void
ir_analysis_dom_frontier_destroy(DominanceFrontier *df)
{

  (void)df;
}

/**
 * @brief 获取指定基本块的支配边界集合。
 */
Bitset *
ir_analysis_dom_frontier_get(DominanceFrontier *df, IRBasicBlock *bb)
{
  DominatorTree *dt = df->dom_tree;
  FunctionCFG *cfg = dt->cfg;


  CFGNode *node = cfg_get_node(cfg, bb);
  if (!node)
  {

    return NULL;
  }


  return df->frontiers[node->id];
}
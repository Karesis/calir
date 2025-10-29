#include "analysis/dom_frontier.h"
#include "analysis/cfg.h"  // 需要 CFGNode, CFGEdge, FunctionCFG
#include "utils/bitset.h"  // 需要 Bitset 操作
#include "utils/bump.h"    // 需要 BUMP_ALLOC_* 宏
#include "utils/id_list.h" // 需要 list_for_each, list_entry

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
  FunctionCFG *cfg = dt->cfg;
  size_t num_blocks = df->num_blocks;

  // 获取当前节点 n 的 Bitset
  // 节点 n 对应的 CFG ID 是 n->cfg_node->id
  Bitset *df_n = df->frontiers[n->cfg_node->id];

  // --- 1. 计算 DF_local (来自 CFG 后继) ---
  // DF(n) = {} (在 compute 函数中已批量零初始化)

  IDList *succ_list = &n->cfg_node->successors;
  IDList *edge_node;
  list_for_each(succ_list, edge_node)
  {
    CFGEdge *edge = list_entry(edge_node, CFGEdge, list_node);
    CFGNode *y_cfg = edge->node; // y 是 n 的一个 CFG 后继

    // 获取 idom(y)
    // dt->nodes[id] 映射 CFG ID 到 DomTreeNode
    DomTreeNode *y_dom_node = dt->nodes[y_cfg->id];
    DomTreeNode *idom_y = y_dom_node->idom;

    if (idom_y != n)
    {
      // idom(y) != n, 所以 y 在 DF_local(n) 中
      bitset_set(df_n, y_cfg->id);
    }
  }

  // --- 2. 计算 DF_up (来自支配树的子节点) ---

  IDList *child_list = &n->children;
  IDList *child_list_node;
  list_for_each(child_list, child_list_node)
  {
    DomTreeChild *child = list_entry(child_list_node, DomTreeChild, list_node);
    DomTreeNode *c = child->node; // c 是 n 在支配树中的一个子节点

    // 1. 首先，递归到底部 (自底向上)
    compute_df_recursive(c, df, temp_set);

    // 2. 现在，处理 c 的支配边界 DF(c)
    Bitset *df_c = df->frontiers[c->cfg_node->id];

    // 我们需要计算: { w | w in DF(c) AND idom(w) != n }
    // 我们使用 temp_set 来暂存这个集合，然后再与 df_n 合并

    bitset_clear_all(temp_set);

    // 遍历 DF(c) 中的所有元素 w
    // (Bitset 没有迭代器，所以我们遍历所有可能的 ID)
    for (size_t w_id = 0; w_id < num_blocks; w_id++)
    {
      if (bitset_test(df_c, w_id))
      {
        // w (ID 为 w_id) 在 DF(c) 中
        DomTreeNode *w_dom_node = dt->nodes[w_id];
        DomTreeNode *idom_w = w_dom_node->idom;

        if (idom_w != n)
        {
          // idom(w) != n, 将 w 添加到 DF_up(n)
          bitset_set(temp_set, w_id);
        }
      }
    }

    // 3. 将这个计算出的集合合并到 DF(n)
    // DF(n) = DF(n) U temp_set
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

  // 1. 分配 DominanceFrontier 结构体
  DominanceFrontier *df = BUMP_ALLOC_ZEROED(arena, DominanceFrontier);
  df->dom_tree = dt;
  df->num_blocks = num_blocks;
  df->arena = arena;

  // 2. 分配 Bitset* 数组 (df->frontiers)
  // BUMP_ALLOC_SLICE_ZEROED 确保所有指针初始化为 NULL (虽然我们马上会覆盖它们)
  df->frontiers = BUMP_ALLOC_SLICE_ZEROED(arena, Bitset *, num_blocks);

  // 3. 为每个基本块创建一个空的 Bitset
  for (size_t i = 0; i < num_blocks; i++)
  {
    // bitset_create 已将所有位初始化为 0 (空集)
    df->frontiers[i] = bitset_create(num_blocks, arena);
  }

  // 4. 创建一个临时 BitSet，供递归函数用于合并
  Bitset *temp_set = bitset_create(num_blocks, arena);

  // 5. 从支配树的根开始，递归计算所有节点的 DF
  compute_df_recursive(dt->root, df, temp_set);

  // 6. temp_set 的内存由 arena 管理，无需单独释放

  return df;
}

/**
 * @brief 释放 DominanceFrontier 结构占用的内存。
 * (通常为空，因为内存由 arena 管理)
 */
void
ir_analysis_dom_frontier_destroy(DominanceFrontier *df)
{
  // 所有内存都在 Bump arena 上分配，由 arena 统一管理
  // (void)df;
}

/**
 * @brief 获取指定基本块的支配边界集合。
 */
Bitset *
ir_analysis_dom_frontier_get(DominanceFrontier *df, IRBasicBlock *bb)
{
  DominatorTree *dt = df->dom_tree;
  FunctionCFG *cfg = dt->cfg;

  // 使用 cfg.h 中定义的辅助函数
  CFGNode *node = cfg_get_node(cfg, bb);
  if (!node)
  {
    // 这个块不在 CFG 中 (例如，如果是孤立块)
    return NULL;
  }

  // CFGNode->id 是 df->frontiers 数组的
  return df->frontiers[node->id];
}
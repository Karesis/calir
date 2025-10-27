// include/analysis/dom_tree.h
#ifndef CALIR_ANALYSIS_DOM_TREE_H
#define CALIR_ANALYSIS_DOM_TREE_H

#include "analysis/cfg.h"
#include "ir/basicblock.h" // For the query API
#include "utils/bump.h"
#include "utils/id_list.h"

// 前向声明
typedef struct DominatorTree DominatorTree;
typedef struct DomTreeNode DomTreeNode;

/**
 * @brief 支配树的子节点
 * 存储在 DomTreeNode 的 'children' 链表中
 */
typedef struct DomTreeChild
{
  DomTreeNode *node; // 指向子节点
  IDList list_node;  // 侵入式链表节点
} DomTreeChild;

/**
 * @brief Lengauer-Tarjan 算法 "bucket" 链表的节点
 * * 存储在 DomTreeNode 的 'bucket' 链表中。
 * bucket[n] 存储所有满足 semi_dom[w] == n 的节点 w
 */
typedef struct BucketNode
{
  DomTreeNode *node; // 指向节点 w
  IDList list_node;  // 侵入式链表节点
} BucketNode;

/**
 * @brief 支配树节点
 */
struct DomTreeNode
{
  CFGNode *cfg_node; // 对应的 CFG 块
  DomTreeNode *idom; // 立即支配者 (父节点)

  // 支配树的子节点 (即被此节点立即支配的节点)
  IDList children; // DomTreeChild 链表 (的头)

  // --- Lengauer-Tarjan 算法所需的临时数据 ---

  // 1. DFS 遍历数据
  DomTreeNode *parent; // DFS 树中的父节点 (不是 idom!)
  int dfs_num;         // DFS 编号 (从 1 开始, 0 代表未访问)

  // 2. Semidominator 数据
  int semi_dom;  // Semidominator (半支配点) 的 *DFS 编号*
  IDList bucket; // BucketNode 链表 (的头)

  // 3. Union-Find (并查集) 数据
  // 用于在计算 semi-dominator 时快速评估
  DomTreeNode *ancestor; // 并查集中的 'ancestor' (用于路径压缩)
  DomTreeNode *label;    // 并查集中的 'label' (路径上 semi_dom 最小的节点)
};

/**
 * @brief 支配树 (Dominator Tree)
 */
struct DominatorTree
{
  FunctionCFG *cfg;
  Bump *arena; // 使用外部传入的竞技场

  DomTreeNode *root; // 支配树的根 (对应 CFG entry)

  // 快速查找:
  // 1. 通过 CFGNode->id 映射到 DomTreeNode*
  DomTreeNode **nodes; // 大小为 cfg->num_nodes

  // 2. 通过 dfs_num (从 1 开始) 映射到 DomTreeNode*
  //    (dfs_order[0] 未使用, dfs_order[1] 是 root)
  DomTreeNode **dfs_order; // 大小为 cfg->num_nodes + 1
};

/**
 * @brief [核心] 使用 Lengauer-Tarjan 算法构建支配树
 *
 * @param cfg CFG
 * @param arena 用于分配所有 DomTreeNode 和内部数据结构的竞技场
 * @return DominatorTree*
 */
DominatorTree *dom_tree_build(FunctionCFG *cfg, Bump *arena);

/**
 * @brief 销毁支配树 (通常为空，因为内存由竞技场管理)
 */
void dom_tree_destroy(DominatorTree *tree);

/**
 * @brief [查询 API] 检查 A 是否支配 B
 *
 * @param tree 支配树
 * @param a 潜在的支配者
 * @param b 潜在的被支配者
 * @return true 如果 a 支配 b
 */
bool dom_tree_dominates(DominatorTree *tree, IRBasicBlock *a, IRBasicBlock *b);

/**
 * @brief [查询 API] 获取 B 的立即支配者
 * @return IRBasicBlock* (如果 b 是入口块，则返回 NULL)
 */
IRBasicBlock *dom_tree_get_idom(DominatorTree *tree, IRBasicBlock *b);

#endif // CALIR_ANALYSIS_DOM_TREE_H
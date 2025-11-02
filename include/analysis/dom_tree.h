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

#ifndef CALIR_ANALYSIS_DOM_TREE_H
#define CALIR_ANALYSIS_DOM_TREE_H

#include "analysis/cfg.h"
#include "ir/basicblock.h"
#include "utils/bump.h"
#include "utils/id_list.h"

typedef struct DominatorTree DominatorTree;
typedef struct DomTreeNode DomTreeNode;

/**
 * @brief 支配树的子节点
 * 存储在 DomTreeNode 的 'children' 链表中
 */
typedef struct DomTreeChild
{
  DomTreeNode *node;
  IDList list_node;
} DomTreeChild;

/**
 * @brief Lengauer-Tarjan 算法 "bucket" 链表的节点
 * * 存储在 DomTreeNode 的 'bucket' 链表中。
 * bucket[n] 存储所有满足 semi_dom[w] == n 的节点 w
 */
typedef struct BucketNode
{
  DomTreeNode *node;
  IDList list_node;
} BucketNode;

/**
 * @brief 支配树节点
 */
struct DomTreeNode
{
  CFGNode *cfg_node;
  DomTreeNode *idom;

  IDList children;

  DomTreeNode *parent;
  int dfs_num;

  int semi_dom;
  IDList bucket;

  DomTreeNode *ancestor;
  DomTreeNode *label;
};

/**
 * @brief 支配树 (Dominator Tree)
 */
struct DominatorTree
{
  FunctionCFG *cfg;
  Bump *arena;

  DomTreeNode *root;

  DomTreeNode **nodes;

  DomTreeNode **dfs_order;
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

#endif
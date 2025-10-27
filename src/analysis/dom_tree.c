// src/analysis/dom_tree.c
#include "analysis/dom_tree.h"
#include "analysis/cfg.h"
#include "ir/value.h" // for IR_KIND_INSTRUCTION etc.
#include "utils/bump.h"
#include "utils/id_list.h"

#include <assert.h>
#include <stdio.h> // for NULL

// -----------------------------------------------------------
// 辅助函数
// -----------------------------------------------------------

/**
 * @brief [辅助] 通过 IRBasicBlock* 获取 DomTreeNode*
 */
static inline DomTreeNode *
get_dom_node(DominatorTree *tree, IRBasicBlock *bb)
{
  if (!bb)
    return NULL;

  // 我们需要从 BB 找到 CFGNode，再找到 DomTreeNode
  CFGNode *cfg_node = cfg_get_node(tree->cfg, bb);
  if (!cfg_node)
  {
    // 这可能发生在 BB 是一个无效块时 (例如 verifier 失败)
    // 或者 BB 是一个参数/常量 (如果查询 API 被误用)
    return NULL;
  }

  // cfg_node->id 是 cfg->nodes 数组的索引
  // tree->nodes 数组使用相同的索引
  return tree->nodes[cfg_node->id];
}

// -----------------------------------------------------------
// Lengauer-Tarjan 算法 (LT-79)
// -----------------------------------------------------------

// --- 步骤 1: DFS 遍历 ---
// 职责:
// 1. 计算 dfs_num (从 1 开始)
// 2. 填充 dfs_order 数组 (通过 dfs_num 索引)
// 3. 填充 parent (DFS 树的父节点)
// 4. 初始化 label (用于 Union-Find)

static void
lt_dfs(DominatorTree *tree, DomTreeNode *n, int *current_dfs_num)
{
  // 1. 分配 DFS 编号
  int n_num = *current_dfs_num;
  *current_dfs_num = n_num + 1;

  n->dfs_num = n_num;
  n->semi_dom = n_num;        // 初始化 semi_dom[n] = n
  n->label = n;               // 初始化 label[n] = n
  tree->dfs_order[n_num] = n; // 填充 dfs_order 映射

  // 2. 遍历 CFG 的后继节点
  IDList *iter;
  list_for_each(&n->cfg_node->successors, iter)
  {
    CFGEdge *succ_edge = list_entry(iter, CFGEdge, list_node);
    CFGNode *succ_cfg_node = succ_edge->node;

    // 通过 CFG id 找到对应的 DomTreeNode
    DomTreeNode *w = tree->nodes[succ_cfg_node->id];

    // 3. 如果 w 未被访问
    if (w->dfs_num == 0)
    {
      w->parent = n; // n 是 w 在 DFS 树中的父节点
      lt_dfs(tree, w, current_dfs_num);
    }
  }
}

// --- 步骤 2: Union-Find (并查集) ---
// 职责:
// 1. union_find_link: 合并两个集合 (将 child 挂到 parent)
// 2. union_find_eval: 查找从 n 到根的路径上 semi_dom 最小的节点

static void
union_find_compress(DomTreeNode *n)
{
  if (n->ancestor->ancestor != NULL)
  {
    union_find_compress(n->ancestor);
    if (n->ancestor->label->semi_dom < n->label->semi_dom)
    {
      n->label = n->ancestor->label;
    }
    n->ancestor = n->ancestor->ancestor;
  }
}

/**
 * @brief 查找从 n (不含) 到根的路径上 semi_dom 最小的节点
 */
static DomTreeNode *
union_find_eval(DomTreeNode *n)
{
  if (n->ancestor == NULL)
  {
    return n->label; // 根节点
  }
  else
  {
    union_find_compress(n);
    // 在 LT 算法中，我们总是需要 semi_dom[label[n]] 最小
    // (原版 LT 返回 n，这里返回 label[n] 是现代优化版)
    return n->label;
  }
}

/**
 * @brief 将子节点 c 链接到父节点 p
 */
static void
union_find_link(DomTreeNode *p, DomTreeNode *c)
{
  c->ancestor = p;
  // (label 的更新在 compress 中处理)
}

// --- 步骤 3: 计算 Semidominators ---
// 职责:
// 1. 按照 DFS 编号从 N 到 2 遍历
// 2. 对每个节点 n, 计算 semi_dom[n]
// 3. 将 n 添加到 semi_dom[n] 的 bucket 中

static void
lt_compute_semi_dominators(DominatorTree *tree)
{
  int num_nodes = tree->cfg->num_nodes;

  // 1. 循环: 按照 DFS 编号从大到小 (N -> 2)
  for (int i = num_nodes; i >= 2; i--)
  {
    DomTreeNode *n = tree->dfs_order[i]; // n 是当前处理的节点
    if (!n)
      continue; // (不应该发生)

    // 2. 遍历 n 的 *所有前驱* v
    IDList *iter;
    list_for_each(&n->cfg_node->predecessors, iter)
    {
      CFGEdge *pred_edge = list_entry(iter, CFGEdge, list_node);
      CFGNode *pred_cfg_node = pred_edge->node;
      DomTreeNode *v = tree->nodes[pred_cfg_node->id];
      if (!v)
        continue;

      // 3. 计算 v'
      // v' = v (如果 v->dfs_num < n->dfs_num)
      // v' = semi_dom 最小的祖先 (如果 v->dfs_num > n->dfs_num)
      DomTreeNode *v_prime;
      if (v->dfs_num <= 0)
        continue; // (前驱未被访问? 不可达? 跳过)

      if (v->dfs_num < n->dfs_num)
      {
        v_prime = v;
      }
      else
      {
        // 查找 v 在 DFS 树上的祖先中，semi_dom 最小的那个
        v_prime = union_find_eval(v);
      }

      // 4. 更新 semi_dom[n]
      // semi_dom[n] = min(semi_dom[n], semi_dom[v'])
      if (v_prime->semi_dom < n->semi_dom)
      {
        n->semi_dom = v_prime->semi_dom;
      }
    }

    // 5. 将 n 添加到 semi_dom[n] 节点的 bucket 中
    //    (semi_dom[n] 此时是一个 dfs_num)
    DomTreeNode *s = tree->dfs_order[n->semi_dom];
    BucketNode *bucket_node = BUMP_ALLOC(&tree->arena, BucketNode);
    bucket_node->node = n;
    list_add_tail(&s->bucket, &bucket_node->list_node);

    // 6. 将 n 链接到其 DFS 父节点
    //    这表示 n 已经被处理过，可用于 eval()
    if (n->parent)
    {
      union_find_link(n->parent, n);
    }
  }
}

// --- 步骤 4: 计算 Idominators (支配树) ---
// 职责:
// 1. 按照 DFS 编号从 2 到 N 遍历
// 2. 利用 semi_dom 和 bucket 计算 idom[n]
// 3. 构建最终的支配树 (children 链表)

static void
lt_compute_idominators(DominatorTree *tree)
{
  int num_nodes = tree->cfg->num_nodes;

  // 1. 循环: 按照 DFS 编号从小到大 (2 -> N)
  for (int i = 2; i <= num_nodes; i++)
  {
    DomTreeNode *n = tree->dfs_order[i];
    if (!n)
      continue;

    DomTreeNode *s = tree->dfs_order[n->semi_dom]; // s = semi_dom[n]

    // 2. 处理 s 的 bucket (所有 semi_dom[w] == s 的节点 w)
    IDList *iter, *temp;
    list_for_each_safe(&s->bucket, iter, temp)
    {
      BucketNode *bucket_node = list_entry(iter, BucketNode, list_node);
      DomTreeNode *w = bucket_node->node;

      // --- 关键定理 ---
      DomTreeNode *u = union_find_eval(w); // u 是 w 祖先中 semi_dom 最小的

      if (u->semi_dom < w->semi_dom)
      {
        // 情况 1: idom[w] = u
        w->idom = u;
      }
      else
      {
        // 情况 2: idom[w] = s (s == semi_dom[w])
        w->idom = s;
      }

      // (我们不需要删除 bucket 节点，因为 arena 会统一清理)
    }
    list_init(&s->bucket); // 清空 bucket
  }

  // --- 步骤 4 (续): 修正 idom ---
  // 入口块没有 idom
  tree->root->idom = NULL;

  // 3. 再次遍历 (2 -> N)，修正 idom
  for (int i = 2; i <= num_nodes; i++)
  {
    DomTreeNode *n = tree->dfs_order[i];
    if (!n)
      continue;

    if (n->idom != tree->dfs_order[n->semi_dom])
    {
      // 修正：idom[n] 应该是 idom[idom[n]]
      n->idom = n->idom->idom;
    }

    // 4. 构建最终的 'children' 树
    if (n->idom)
    {
      DomTreeChild *child_node = BUMP_ALLOC(&tree->arena, DomTreeChild);
      child_node->node = n;
      list_add_tail(&n->idom->children, &child_node->list_node);
    }
  }
}

// -----------------------------------------------------------
// 公共 API 实现
// -----------------------------------------------------------

DominatorTree *
dom_tree_build(FunctionCFG *cfg, Bump *arena)
{
  if (!cfg || !cfg->entry_node)
  {
    return NULL; // 无法处理空 CFG
  }

  int num_nodes = cfg->num_nodes;

  // 1. 分配 DominatorTree 结构体
  DominatorTree *tree = BUMP_ALLOC_ZEROED(arena, DominatorTree);
  tree->cfg = cfg;
  tree->arena = arena;

  // 2. 分配所有辅助数组
  // tree->nodes (索引: 0..N-1)
  tree->nodes = BUMP_ALLOC_SLICE_ZEROED(arena, DomTreeNode *, num_nodes);

  // tree->dfs_order (索引: 1..N, 0 号不用)
  tree->dfs_order = BUMP_ALLOC_SLICE_ZEROED(arena, DomTreeNode *, num_nodes + 1);

  // 3. 为每个 CFGNode 分配一个 DomTreeNode
  for (int i = 0; i < num_nodes; i++)
  {
    CFGNode *cfg_node = &cfg->nodes[i];
    DomTreeNode *dom_node = BUMP_ALLOC_ZEROED(arena, DomTreeNode);

    // 初始化节点
    dom_node->cfg_node = cfg_node;
    dom_node->dfs_num = 0; // 0 = 未访问
    list_init(&dom_node->children);
    list_init(&dom_node->bucket);

    // label 初始化为
    dom_node->label = dom_node;

    // 填充 nodes 映射
    tree->nodes[i] = dom_node;
  }

  // 4. 设置根节点
  tree->root = tree->nodes[cfg->entry_node->id];

  // --- 执行 Lengauer-Tarjan 算法 ---

  // 步骤 1: DFS 遍历
  int dfs_counter = 1;
  lt_dfs(tree, tree->root, &dfs_counter);

  // (如果 dfs_counter <= num_nodes，说明有不可达块，
  //  L-T 算法会自动处理它们，它们不会在支配树中)

  // 步骤 2 & 3: 计算 Semidominators (核心)
  lt_compute_semi_dominators(tree);

  // 步骤 4: 计算 Idominators (收尾)
  lt_compute_idominators(tree);

  return tree;
}

void
dom_tree_destroy(DominatorTree *tree)
{
  // 无需操作。
  // DominatorTree 结构体自身、
  // tree->nodes 数组、
  // tree->dfs_order 数组、
  // 所有的 DomTreeNode、
  // 所有的 DomTreeChild、
  // 所有的 BucketNode
  // ... 全部都在传入的 *arena* 上分配，
  // 将由 verifier 中的 bump_destroy(&vctx.analysis_arena) 统一释放。
}

bool
dom_tree_dominates(DominatorTree *tree, IRBasicBlock *a, IRBasicBlock *b)
{
  // 规则 1: 处理非指令/非块的定义 (常量, 参数)
  // 它们支配所有块
  if (a->label_address.kind != IR_KIND_BASIC_BLOCK)
  {
    return true;
  }

  // 规则 2: 一个块总是支配它自己
  if (a == b)
  {
    return true;
  }

  DomTreeNode *node_a = get_dom_node(tree, a);
  DomTreeNode *node_b = get_dom_node(tree, b);

  if (!node_a || !node_b)
  {
    // 如果 B 是不可达块 (不在 DomTree 中)
    if (!node_b && b->label_address.kind == IR_KIND_BASIC_BLOCK)
    {
      return false; // 不可达块不被任何东西支配 (除了它自己，已在规则2处理)
    }
    // A 或 B 是无效块
    return false;
  }

  // 规则 3: A 是否是 B 在支配树上的祖先?
  // 向上遍历 B 的支配树，看是否能碰到 A
  DomTreeNode *current = node_b->idom; // 从 B 的父节点开始
  while (current)
  {
    if (current == node_a)
    {
      return true; // 找到了 A，A 支配 B
    }
    current = current->idom; // 移动到 B 的立即支配者
  }

  return false; // 遍历到根也没找到 A，A 不支配 B
}

IRBasicBlock *
dom_tree_get_idom(DominatorTree *tree, IRBasicBlock *b)
{
  DomTreeNode *node_b = get_dom_node(tree, b);

  if (node_b && node_b->idom)
  {
    return node_b->idom->cfg_node->block;
  }

  return NULL; // b 是入口块, 或者 b 是无效块
}
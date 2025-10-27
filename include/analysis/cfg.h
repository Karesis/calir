// include/analysis/cfg.h
#ifndef CALIR_ANALYSIS_CFG_H
#define CALIR_ANALYSIS_CFG_H

#include "ir/basicblock.h"
#include "ir/function.h"
#include "ir/instruction.h" // 需要访问终结者指令
#include "utils/bump.h"
#include "utils/hashmap.h" // PtrHashMap
#include "utils/id_list.h"

// 前向声明
typedef struct CFGNode CFGNode;

/**
 * @brief CFG 图中的一条边
 * 存储在 CFGNode 的 successors/predecessors 链表中
 */
typedef struct CFGEdge
{
  CFGNode *node;    // 指向目标 (succ) 或来源 (pred) 节点
  IDList list_node; // 侵入式链表节点
} CFGEdge;

/**
 * @brief CFG 图中的一个节点
 */
struct CFGNode
{
  IRBasicBlock *block; // 指向原始 BB
  int id;              // 稠密 ID (从 0 到 N-1)

  // 存储 CFGEdge 结构体
  IDList successors;   // CFGEdge 链表 (的头)
  IDList predecessors; // CFGEdge 链表 (的头)
};

// 整个函数的 CFG
typedef struct FunctionCFG
{
  IRFunction *func;
  int num_nodes;

  // 竞技场，用于分配所有 CFGNode, CFGEdge 和 nodes 数组
  Bump arena;

  // 存储所有 CFGNode 的数组，通过 node->id 索引
  CFGNode *nodes;

  CFGNode *entry_node; // 入口块 (id == 0)

  // 从 IRBasicBlock* 快速映射到 CFGNode*
  PtrHashMap *block_to_node_map;

} FunctionCFG;

/**
 * @brief 为一个函数构建控制流图 (CFG)
 *
 * @param func 函数
 * @param arena 用于 FunctionCFG 结构 *自身* 分配的竞技场 (其内部节点将使用 *自己* 的 arena)
 * @return FunctionCFG*
 */
FunctionCFG *cfg_build(IRFunction *func, Bump *arena);

/**
 * @brief 销毁 CFG (释放其内部竞技场和 hashmap)
 */
void cfg_destroy(FunctionCFG *cfg);

/**
 * @brief [辅助函数] 通过 IRBasicBlock* 获取 CFGNode*
 */
static inline CFGNode *
cfg_get_node(FunctionCFG *cfg, IRBasicBlock *bb)
{
  return (CFGNode *)ptr_hashmap_get(cfg->block_to_node_map, bb);
}

#endif // CALIR_ANALYSIS_CFG_H
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

#ifndef CALIR_ANALYSIS_CFG_H
#define CALIR_ANALYSIS_CFG_H

#include "ir/basicblock.h"
#include "ir/function.h"
#include "ir/instruction.h"
#include "utils/bump.h"
#include "utils/hashmap.h"
#include "utils/id_list.h"

typedef struct CFGNode CFGNode;

/**
 * @brief CFG 图中的一条边
 * 存储在 CFGNode 的 successors/predecessors 链表中
 */
typedef struct CFGEdge
{
  CFGNode *node;
  IDList list_node;
} CFGEdge;

/**
 * @brief CFG 图中的一个节点
 */
struct CFGNode
{
  IRBasicBlock *block;
  int id;

  IDList successors;
  IDList predecessors;
};

typedef struct FunctionCFG
{
  IRFunction *func;
  int num_nodes;

  Bump arena;

  CFGNode *nodes;

  CFGNode *entry_node;

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

#endif
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

#ifndef CALIR_ANALYSIS_DOM_FRONTIER_H
#define CALIR_ANALYSIS_DOM_FRONTIER_H

#include "analysis/dom_tree.h"
#include "ir/basicblock.h"
#include "ir/function.h"
#include "utils/bitset.h"
#include "utils/bump.h"

/**
 * @struct DominanceFrontier
 * @brief 存储函数中所有基本块的支配边界 (Dominance Frontier) 集合。
 *
 * 对于每个基本块 B，它的支配边界 DF(B) 是这样一个块 Y 的集合：
 * B 支配 Y 的一个前驱，但 B 并不严格支配 Y。
 *
 * 这个结构主要用于 SSA 构造（mem2reg），以确定在何处插入 PHI 节点。
 */
typedef struct DominanceFrontier
{
  /** * 指向此 DF 所依赖的支配树。
   * 我们需要它来将 IRBasicBlock* 映射到它们的 ID。
   */
  DominatorTree *dom_tree;

  /**
   * 支配边界集合的数组。
   * 这是一个大小为 N (函数中的块数) 的数组，其中每个元素是一个 BitSet。
   * frontiers[i] 存储的是 ID 为 i 的基本块的支配边界集合。
   */
  Bitset **frontiers;
  size_t num_blocks;

  Bump *arena;

} DominanceFrontier;

/**
 * @brief 计算给定函数的支配边界。
 * @param dt 为该函数预先计算好的支配树。
 * @param arena 用于分配 DominanceFrontier 结构及其内部数据的内存。
 * @return 一个新的 DominanceFrontier 实例。
 */
DominanceFrontier *ir_analysis_dom_frontier_compute(DominatorTree *dt, Bump *arena);

/**
 * @brief 释放 DominanceFrontier 结构占用的内存。
 * @param df 要释放的 DominanceFrontier 实例。
 * @note 这不会释放 arena 本身。
 */
void ir_analysis_dom_frontier_destroy(DominanceFrontier *df);

/**
 * @brief 获取指定基本块的支配边界集合。
 * @param df 计算好的 DominanceFrontier 实例。
 * @param bb 要查询的基本块。
 * @return 一个 (const) BitSet，表示 bb 的支配边界。
 * 如果块无效，则返回 NULL。
 */
Bitset *ir_analysis_dom_frontier_get(DominanceFrontier *df, IRBasicBlock *bb);

#endif
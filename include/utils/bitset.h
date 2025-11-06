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

#pragma once

#include "utils/bump.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief 一个高密度位集，用于数据流分析
 */
typedef struct Bitset
{
  size_t num_bits;
  size_t num_words;
  uint64_t *words;
} Bitset;

/**
 * @brief 创建一个新的、所有位都为 0 的位集
 *
 * @param num_bits 集合中所需的位数
 * @param arena 用于分配 Bitset 结构体和其内部 words 数组的竞技场
 * @return Bitset* (已在竞技场上分配)
 */
Bitset *bitset_create(size_t num_bits, Bump *arena);

/**
 * @brief 创建一个新的、所有位都为 1 的位集 (全集)
 *
 * @param num_bits 集合中所需的位数
 * @param arena 竞技场
 * @return Bitset*
 */
Bitset *bitset_create_all(size_t num_bits, Bump *arena);

/**
 * @brief 将指定位设置为 1 (添加到集合)
 */
void bitset_set(Bitset *bs, size_t bit);

/**
 * @brief 将指定位设置为 0 (从集合中移除)
 */
void bitset_clear(Bitset *bs, size_t bit);

/**
 * @brief 检查指定位是否为 1 (是否在集合中)
 */
bool bitset_test(const Bitset *bs, size_t bit);

/**
 * @brief 将所有位设置为 1 (全集)
 * @note 只设置 num_bits 范围内的位
 */
void bitset_set_all(Bitset *bs);

/**
 * @brief 将所有位设置为 0 (空集)
 */
void bitset_clear_all(Bitset *bs);

/**
 * @brief 检查两个位集是否相等
 * @note 必须具有相同的 num_bits
 */
bool bitset_equals(const Bitset *bs1, const Bitset *bs2);

/**
 * @brief 复制位集 (dest = src)
 * @note dest 和 src 必须具有相同的 num_bits
 */
void bitset_copy(Bitset *dest, const Bitset *src);

/**
 * @brief 计算两个位集的交集 (dest = src1 ∩ src2)
 * @note 所有位集必须具有相同的 num_bits
 */
void bitset_intersect(Bitset *dest, const Bitset *src1, const Bitset *src2);

/**
 * @brief 计算两个位集的并集 (dest = src1 U src2)
 * @note 所有位集必须具有相同的 num_bits
 */
void bitset_union(Bitset *dest, const Bitset *src1, const Bitset *src2);

/**
 * @brief 计算两个位集的差集 (dest = src1 \ src2) (即 dest = src1 AND (NOT src2))
 * @note 所有位集必须具有相同的 num_bits
 */
void bitset_difference(Bitset *dest, const Bitset *src1, const Bitset *src2);

/**
 * @brief [调试用] 统计集合中 1 的数量 (较慢)
 */
size_t bitset_count_slow(const Bitset *bs);

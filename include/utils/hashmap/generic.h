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


/* include/utils/hashmap/generic.h */
#ifndef HASHMAP_GENERIC_H
#define HASHMAP_GENERIC_H

#include "utils/bump.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief 用于 'GenericHashMap' 的哈希函数指针类型。
 * @param key 指向要哈希的 key (struct) 的 void* 指针。
 * @return 64位哈希值。
 */
typedef uint64_t (*GenericHashFn)(const void *key);

/**
 * @brief 用于 'GenericHashMap' 的相等比较函数指针类型。
 * @param key1 指向第一个 key (struct) 的 void* 指针。
 * @param key2 指向第二个 key (struct) 的 void* 指针。
 * @return true 如果两个 key 相等, 否则 false。
 */
typedef bool (*GenericEqualFn)(const void *key1, const void *key2);

/**
 * @brief 一个高性能、开放地址、使用函数指针的泛型哈希表。
 *
 * - 它使用 Bump Allocator 进行所有内存分配。
 * - Keys (指针) 被直接存储, *不会*被复制。
 * - Values 存储为 void*。
 * - 哈希和比较逻辑由用户在创建时通过函数指针提供。
 */

// 不透明的泛型哈希表结构体
typedef struct GenericHashMap GenericHashMap;

/**
 * @brief 创建一个新的 GenericHashMap。
 *
 * @param arena 要用于所有分配的 Bump Allocator。
 * @param initial_capacity 预期的最小条目数。
 * @param hash_fn 用于哈希 key (void*) 的函数指针。
 * @param equal_fn 用于比较两个 key (void*) 是否相等的函数指针。
 * @return GenericHashMap* 成功则返回指向新 Map 的指针，失败 (OOM) 返回 NULL。
 */
GenericHashMap *generic_hashmap_create(Bump *arena, size_t initial_capacity, GenericHashFn hash_fn,
                                       GenericEqualFn equal_fn);

/**
 * @brief 插入或更新一个键值对。
 *
 * @param map 哈希表。
 * @param key 作为 Key 的 void* 指针 (指向你的 struct)。
 * @param value 要存储的 void* 值。
 * @return bool true 表示成功，false 表示内存溢出 (在扩容时)。
 */
bool generic_hashmap_put(GenericHashMap *map, const void *key, void *value);

/**
 * @brief 查找一个 Key 对应的 Value。
 *
 * @param map 哈希表。
 * @param key 要查找的 void* 指针 (指向你的 struct)。
 * @return void* 如果找到，返回存储的 Value；否则返回 NULL。
 */
void *generic_hashmap_get(const GenericHashMap *map, const void *key);

/**
 * @brief 从哈希表中移除一个 Key。
 *
 * @param map 哈希表。
 * @param key 要移除的 void* 指针 (指向你的 struct)。
 * @return bool true 表示 Key 被找到并移除，false 表示 Key 不存在。
 */
bool generic_hashmap_remove(GenericHashMap *map, const void *key);

/**
 * @brief 检查一个 Key 是否存在。
 *
 * @param map 哈希表。
 * @param key 要检查的 void* 指针 (指向你的 struct)。
 * @return bool true 表示 Key 存在，false 表示不存在。
 */
bool generic_hashmap_contains(const GenericHashMap *map, const void *key);

/**
 * @brief 获取哈希表中的条目数。
 *
 * @param map 哈希表。
 * @return size_t 条目数。
 */
size_t generic_hashmap_size(const GenericHashMap *map);

/*
 * ========================================
 * --- 迭代器 API ---
 * ========================================
 */

/** @brief GenericHashMap 的一个条目 (Key/Value 对) */
typedef struct
{
  const void *key; // Key 是一个 void*
  void *value;
} GenericHashMapEntry;

/** @brief GenericHashMap 的迭代器状态 */
typedef struct
{
  const GenericHashMap *map;
  size_t index; /* 内部桶数组的当前索引 */
} GenericHashMapIter;

/**
 * @brief 初始化一个哈希表迭代器。
 */
GenericHashMapIter generic_hashmap_iter(const GenericHashMap *map);

/**
 * @brief 推进迭代器并获取下一个条目。
 * @return true 如果找到了下一个条目, entry_out 被填充。
 * @return false 如果已到达末尾。
 */
bool generic_hashmap_iter_next(GenericHashMapIter *iter, GenericHashMapEntry *entry_out);

#endif // HASHMAP_GENERIC_H
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


/* hashmap/_ptr.h */
#ifndef HASHMAP_PTR_H
#define HASHMAP_PTR_H

#include "utils/bump.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * 一个高性能、开放地址、二次探测的哈希表。
 * [专用于 void* 指针 Key]
 *
 * - 它使用 Bump Allocator 进行所有内存分配。
 * - Keys (指针) 被直接存储，*不会*被复制。
 * - Values 存储为 void*，由调用者管理其生命周期。
 */


typedef struct PtrHashMap PtrHashMap;


typedef struct
{
  void *key;
  void *value;
} PtrHashMapEntry;

typedef struct
{
  const PtrHashMap *map;
  size_t index;
} PtrHashMapIter;

/**
 * @brief 创建一个新的 PtrHashMap。
 *
 * 分配 PtrHashMap 结构体本身及其初始桶都来自 Arena。
 *
 * @param arena 要用于所有分配的 Bump Allocator。
 * @param initial_capacity 预期的最小条目数。将被向上取整到 2 的幂。
 * @return PtrHashMap* 成功则返回指向新 Map 的指针，失败 (OOM) 返回 NULL。
 */
PtrHashMap *ptr_hashmap_create(Bump *arena, size_t initial_capacity);

/**
 * @brief 插入或更新一个键值对。
 *
 * 如果 Key (指针) 不存在，它将被插入。
 * 如果 Key 已存在，它的 Value 将被更新。
 *
 * @param map 哈希表。
 * @param key 作为 Key 的 void* 指针。
 * @param value 要存储的 void* 值。
 * @return bool true 表示成功，false 表示内存溢出 (在扩容时)。
 */
bool ptr_hashmap_put(PtrHashMap *map, void *key, void *value);

/**
 * @brief 查找一个 Key 对应的 Value。
 *
 * @param map 哈希表。
 * @param key 要查找的 void* 指针。
 * @return void* 如果找到，返回存储的 Value；否则返回 NULL。
 */
void *ptr_hashmap_get(const PtrHashMap *map, void *key);

/**
 * @brief 从哈希表中移除一个 Key。
 *
 * @param map 哈希表。
 * @param key 要移除的 void* 指针。
 * @return bool true 表示 Key 被找到并移除，false 表示 Key 不存在。
 */
bool ptr_hashmap_remove(PtrHashMap *map, void *key);

/**
 * @brief 检查一个 Key 是否存在。
 *
 * @param map 哈希表。
 * @param key 要检查的 void* 指针。
 * @return bool true 表示 Key 存在，false 表示不存在。
 */
bool ptr_hashmap_contains(const PtrHashMap *map, void *key);

/**
 * @brief 获取哈希表中的条目数。
 *
 * @param map 哈希表。
 * @return size_t 条目数。
 */
size_t ptr_hashmap_size(const PtrHashMap *map);


PtrHashMapIter ptr_hashmap_iter(const PtrHashMap *map);
bool ptr_hashmap_iter_next(PtrHashMapIter *iter, PtrHashMapEntry *entry_out);

#endif

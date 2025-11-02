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


/* include/hashmap/str_slice.h */
#ifndef HASHMAP_STR_SLICE_H
#define HASHMAP_STR_SLICE_H

#include "utils/bump.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * 一个高性能、开放地址、二次探测的哈希表。
 * [专用于 C 字符串切片 Key (char*, size_t)]
 *
 * - 它使用 Bump Allocator 进行所有内存分配。
 * - Keys (字符串) 在 'put' 时被复制到 Arena 中。
 * - Values 存储为 void*，由调用者管理其生命周期。
 */

// 不透明的字符串哈希表结构体
typedef struct StrHashMap StrHashMap;

// 迭代器结构体 (匹配公共 API)
typedef struct
{
  const char *key_body;
  size_t key_len;
  void *value;
} StrHashMapEntry;

typedef struct
{
  const StrHashMap *map;
  size_t index;
} StrHashMapIter;

/**
 * @brief 创建一个新的 StrHashMap。
 *
 * 分配 StrHashMap 结构体本身及其初始桶都来自 Arena。
 *
 * @param arena 要用于所有分配的 Bump Allocator。
 * @param initial_capacity 预期的最小条目数。将被向上取整到 2 的幂。
 * @return StrHashMap* 成功则返回指向新 Map 的指针，失败 (OOM) 返回 NULL。
 */
StrHashMap *str_hashmap_create(Bump *arena, size_t initial_capacity);

/**
 * @brief 插入或更新一个键值对。
 *
 * Key (由 key_body 和 key_len 定义的字符串) 将被复制到 Arena 中。
 * 如果 Key 不存在，它将被插入。
 * 如果 Key 已存在，它的 Value 将被更新。
 *
 * @param map 哈希表。
 * @param key_body 指向字符串内容的指针。
 * @param key_len 字符串的长度。
 * @param value 要存储的 void* 值。
 * @return bool true 表示成功，false 表示内存溢出 (在扩容或复制 Key 时)。
 */
bool str_hashmap_put(StrHashMap *map, const char *key_body, size_t key_len, void *value);

/**
 * @brief 插入一个*已经*在 Arena 中的键 (专用于 Interning)
 *
 * 此函数*假定* key_body 指向的内存*已经*
 * 位于 hashmap 自己的 Arena 中，并且生命周期相同。
 *
 * 它*不会*复制 key_body, 而是直接存储 key_body 指针
 * 作为内部的 Key。
 *
 * @param map 哈希表。
 * @param key_body 指向 Arena 中字符串内容的指针。
 * @param key_len 字符串的长度。
 * @param value 要存储的 void* 值。
 * @return bool true 表示成功，false 表示内存溢出 (在扩容时)。
 */
bool str_hashmap_put_preallocated_key(StrHashMap *map, const char *key_body, size_t key_len, void *value);

/**
 * @brief 查找一个 Key 对应的 Value。
 *
 * @param map 哈希表。
 * @param key_body 要查找的字符串指针。
 * @param key_len 要查找的字符串长度。
 * @return void* 如果找到，返回存储的 Value；否则返回 NULL。
 */
void *str_hashmap_get(const StrHashMap *map, const char *key_body, size_t key_len);

/**
 * @brief 从哈希表中移除一个 Key。
 *
 * @param map 哈希表。
 * @param key_body 要移除的字符串指针。
 * @param key_len 要移除的字符串长度。
 * @return bool true 表示 Key 被找到并移除，false 表示 Key 不存在。
 */
bool str_hashmap_remove(StrHashMap *map, const char *key_body, size_t key_len);

/**
 * @brief 检查一个 Key 是否存在。
 *
 * @param map 哈希表。
 * @param key_body 要检查的字符串指针。
 * @param key_len 要检查的字符串长度。
 * @return bool true 表示 Key 存在，false 表示不存在。
 */
bool str_hashmap_contains(const StrHashMap *map, const char *key_body, size_t key_len);

/**
 * @brief 获取哈希表中的条目数。
 *
 * @param map 哈希表。
 * @return size_t 条目数。
 */
size_t str_hashmap_size(const StrHashMap *map);

// 迭代器函数声明
StrHashMapIter str_hashmap_iter(const StrHashMap *map);
bool str_hashmap_iter_next(StrHashMapIter *iter, StrHashMapEntry *entry_out);

#endif // HASHMAP_STR_SLICE_H
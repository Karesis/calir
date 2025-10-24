/* hashmap/_str_slice.h */
#ifndef HASHMAP_STR_SLICE_H
#define HASHMAP_STR_SLICE_H

#include "utils/bump.h" // 你的 Bump Allocator
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * 一个高性能、开放地址、二次探测的哈希表。
 * [专用于 (char*, size_t) 字符串切片 Key]
 *
 * - 它使用 Bump Allocator 进行所有内存分配。
 * - Keys 在插入时会被复制到 Bump Arena 中，由哈希表管理其生命周期。
 * - Values 存储为 void*，由调用者管理其生命周期。
 */

// 不透明的字符串哈希表结构体
typedef struct StrHashMap_t StrHashMap;

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
 * 如果 Key 不存在，它将被*复制*到 Arena 中并插入。
 * 如果 Key 已存在，它的 Value 将被更新。
 *
 * @param map 哈希表。
 * @param key_body 指向 Key 数据的指针。
 * @param key_len Key 的长度 (字节)。
 * @param value 要存储的 void* 值。
 * @return bool true 表示成功，false 表示内存溢出 (在扩容或复制 Key 时)。
 */
bool str_hashmap_put(StrHashMap *map, const char *key_body, size_t key_len, void *value);

/**
 * @brief 查找一个 Key 对应的 Value。
 *
 * @param map 哈希表。
 * @param key_body 指向 Key 数据的指针。
 * @param key_len Key 的长度 (字节)。
 * @return void* 如果找到，返回存储的 Value；否则返回 NULL。
 */
void *str_hashmap_get(const StrHashMap *map, const char *key_body, size_t key_len);

/**
 * @brief 从哈希表中移除一个 Key。
 *
 * @param map 哈希表。
 * @param key_body 指向 Key 数据的指针。
 * @param key_len Key 的长度 (字节)。
 * @return bool true 表示 Key 被找到并移除，false 表示 Key 不存在。
 */
bool str_hashmap_remove(StrHashMap *map, const char *key_body, size_t key_len);

/**
 * @brief 检查一个 Key 是否存在。
 *
 * @param map 哈希表。
 * @param key_body 指向 Key 数据的指针。
 * @param key_len Key 的长度 (字节)。
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

#endif // HASHMAP_STR_SLICE_H
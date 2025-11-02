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

/*
 * [Note] The bump-pointer allocation strategy used here was inspired by
 * learning about the 'arena' allocation model, particularly from studying
 * the design of the Rust 'bumpalo' library.
 * `bumpalo` library: https://github.com/fitzgen/bumpalo
 */

#ifndef BUMP_H
#define BUMP_H

#include <stdbool.h> // for bool
#include <stddef.h>  // for size_t
#include <stdint.h>  // for SIZE_MAX, uintptr_t
#include <string.h>

/*
 * * 内存布局:
 * [------------------ data ------------------][-- ChunkFooter --]
 * ^                                          ^                 ^
 * |                                          |                 |
 * data                                       ptr         (data + chunk_size)
 * (由 malloc 返回)                         (碰撞指针)
 *
 * 碰撞指针(ptr) 从 (data + chunk_size - sizeof(ChunkFooter)) 开始，
 * 向 data 方向（低地址）移动。
 */
typedef struct ChunkFooter ChunkFooter;
struct ChunkFooter
{
  // 指向此 Chunk 内存的起始地址 (即 malloc 返回的指针)
  unsigned char *data;

  // 此 Chunk 的总大小 (即 malloc 分配的大小)
  size_t chunk_size;

  // 指向上一个 Chunk 的 Footer
  ChunkFooter *prev;

  // 碰撞指针。指向下一个分配将开始的*末尾*地址。
  // 它从 (unsigned char*)this - sizeof(ChunkFooter) 开始，向 data 移动。
  unsigned char *ptr;

  // 到此 Chunk 为止，所有 Chunk 的 *可用* 空间总和
  // (不包括 Footer 自身)
  size_t allocated_bytes;
};

/*
 * 内存布局 (Layout) 结构
 * 模仿 Rust 的 core::alloc::Layout
 */
typedef struct
{
  size_t size;
  size_t align;
} BumpLayout;

/*
 * 对应 Rust 的 Bump
 */
typedef struct
{
  // 指向当前正在使用的 Chunk 的 Footer
  ChunkFooter *current_chunk_footer;

  // 分配上限 (SIZE_MAX 表示无上限)
  size_t allocation_limit;

  // 最小对齐。
  // 对应 Rust 的 const MIN_ALIGN。在 C 中我们将其作为运行时字段。
  size_t min_align;
} Bump;

/*
 * --- 生命周期 ---
 */

/**
 * @brief 在堆上创建一个新的 Bump Arena。
 * * @param min_align 最小对齐。所有分配都将至少以此对齐。
 * 必须是 2 的幂，且不大于 16。
 * 传 1 表示默认对齐。
 * @return Bump* 成功则返回指向新 Arena 的指针，失败返回 NULL。
 * @note 用户必须调用 bump_free() 来释放。
 */
Bump *bump_new_with_min_align(size_t min_align);

/**
 * @brief 在堆上创建一个新的 Bump Arena (最小对齐为 1)。
 *
 * @return Bump* 成功则返回指向新 Arena 的指针，失败返回 NULL。
 * @note 用户必须调用 bump_free() 来释放。
 */
Bump *bump_new(void);

/**
 * @brief 初始化一个已分配的 Bump 结构 (例如在栈上)。
 * * @param bump 指向要初始化的 Bump 实例的指针。
 * @param min_align 最小对齐。
 */
void bump_init_with_min_align(Bump *bump, size_t min_align);

/**
 * @brief 初始化一个已分配的 Bump 结构 (最小对齐为 1)。
 * * @param bump 指向要初始化的 Bump 实例的指针。
 */
void bump_init(Bump *bump);

/**
 * @brief 销毁 Arena，释放其分配的所有内存块。
 * @note 这 *不会* 释放 bump 结构体本身。
 * 这允许 bump_init/bump_destroy 用于栈分配的 Arena。
 *
 * @param bump 要销毁的 Arena。
 */
void bump_destroy(Bump *bump);

/**
 * @brief 销毁 Arena 并释放 Bump 结构体本身。
 * @note 只能用于由 bump_new() 创建的 Arena。
 *
 * @param bump 要释放的 Arena。
 */
void bump_free(Bump *bump);

/**
 * @brief 重置 Arena。
 *
 * 释放所有已分配的 Chunk (除了当前 Chunk)，
 * 并将当前 Chunk 的碰撞指针重置。
 * 之后可以重用 Arena。
 *
 * @param bump 要重置的 Arena。
 */
void bump_reset(Bump *bump);

/*
 * --- 分配 API ---
 */

/**
 * @brief 核心分配函数。
 *
 * 在 Arena 中分配一块具有指定布局 (大小和对齐) 的内存。
 * 内存是 *未初始化* 的。
 *
 * @param bump Arena。
 * @param layout 内存布局 (大小和对齐)。
 * @return void* 成功则返回指向已分配内存的指针，失败 (OOM) 返回 NULL。
 */
void *bump_alloc_layout(Bump *bump, BumpLayout layout);

/**
 * @brief 便捷函数：按大小和对齐分配。
 *
 * @param bump Arena。
 * @param size 要分配的字节数。
 * @param align 内存对齐 (必须是 2 的幂)。
 * @return void* 成功则返回指针，失败返回 NULL。
 */
void *bump_alloc(Bump *bump, size_t size, size_t align);

/**
 * @brief 便捷函数：分配并复制数据。(memcpy风格)
 *
 * @param bump Arena。
 * @param src 要复制的数据源。
 * @param size 要分配和复制的字节数。
 * @param align 内存对齐 (必须是 2 的幂)。
 * @return void* 成功则返回指向 Arena 中新副本的指针，失败返回 NULL。
 */
void *bump_alloc_copy(Bump *bump, const void *src, size_t size, size_t align);

/**
 * @brief 便捷函数：分配并复制一个字符串。
 *
 * @param bump Arena。
 * @param str 要复制的 C 字符串 (以 '\0' 结尾)。
 * @return char* 成功则返回指向 Arena 中新字符串的指针，失败返回 NULL。
 */
char *bump_alloc_str(Bump *bump, const char *str);

/**
 * @brief 重新分配一块内存 (分配 + 复制)。
 *
 * 在 arena 中，这*几乎*总是会分配一块 *新* 内存，
 * 复制 [old_ptr, old_ptr + old_size] 的内容，
 * 并 "泄露" (abandon) 旧的 [old_ptr] 内存块。
 *
 * @param bump Arena。
 * @param old_ptr 指向要 "重新分配" 的旧内存块。如果为 NULL，则等同于 bump_alloc。
 * @param old_size *旧内存块中要复制的数据大小*。
 * @param new_size *要分配的新内存块的总大小*。
 * @param align 新内存块的对齐方式。
 * @return void* 成功则返回指向*新*内存块的指针，失败返回 NULL。
 */
void *bump_realloc(Bump *bump, void *old_ptr, size_t old_size, size_t new_size, size_t align);

/*
 * --- 容量和限制 ---
 */

/**
 * @brief 设置分配上限 (字节)。
 *
 * @param bump Arena。
 * @param limit 新的上限。传入 SIZE_MAX 表示无限制。
 */
void bump_set_allocation_limit(Bump *bump, size_t limit);

/**
 * @brief 获取当前已分配的 *可用* 字节总数 (所有 Chunk 的容量总和)。
 *
 * @param bump Arena。
 * @return size_t 字节数。
 */
size_t bump_get_allocated_bytes(Bump *bump);

/**
 * @brief 分配单个 T 实例
 *
 * @param bump_ptr (Bump *) Arena 指针。
 * [!!!] 注意：传入 (Bump *)，不要传入 (Bump **)。
 * 例如：BUMP_ALLOC(my_arena, ...)  [正确]
 * BUMP_ALLOC(&my_arena, ...) [错误]
 * @param T 要分配的类型
 */
#define BUMP_ALLOC(bump_ptr, T) ((T *)bump_alloc((bump_ptr), sizeof(T), _Alignof(T)))

// 分配 T 的数组（未初始化）
#define BUMP_ALLOC_SLICE(bump_ptr, T, count) ((T *)bump_alloc((bump_ptr), sizeof(T) * (count), _Alignof(T)))

// 分配并复制 T 的数组
#define BUMP_ALLOC_SLICE_COPY(bump_ptr, T, src_ptr, count)                                                             \
  ((T *)bump_alloc_copy((bump_ptr), (src_ptr), sizeof(T) * (count), _Alignof(T)))

// 分配并零初始化单个 T 实例
#define BUMP_ALLOC_ZEROED(bump_ptr, T) ((T *)memset(BUMP_ALLOC((bump_ptr), T), 0, sizeof(T)))

// 分配并零初始化 T 的数组
#define BUMP_ALLOC_SLICE_ZEROED(bump_ptr, T, count)                                                                    \
  ((T *)memset(BUMP_ALLOC_SLICE((bump_ptr), T, (count)), 0, sizeof(T) * (count)))

/**
 * @brief 重新分配 T 的数组（分配 + 复制）。
 *
 * @param bump_ptr Arena 指针。
 * @param T 元素类型。
 * @param old_ptr 指向旧的 slice。
 * @param old_count 要复制的 *元素* 数量 (来自旧 slice)。
 * @param new_count 要分配的 *元素* 数量 (用于新 slice)。
 */
#define BUMP_REALLOC_SLICE(bump_ptr, T, old_ptr, old_count, new_count)                                                 \
  ((T *)bump_realloc((bump_ptr), (old_ptr), sizeof(T) * (old_count), sizeof(T) * (new_count), _Alignof(T)))

#endif
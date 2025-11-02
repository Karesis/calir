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

/* bump.c */
#include "utils/bump.h"
#include <assert.h> // for assert
#include <stdio.h>  // for fprintf, stderr
#include <stdlib.h> // for malloc, free
#include <string.h> // for memcpy, strlen

/*
 * --- 用于调试的宏 ---
 */
#define assert_print(condition, message)                                                                               \
  do                                                                                                                   \
  {                                                                                                                    \
    if (!(condition))                                                                                                  \
    {                                                                                                                  \
      fprintf(stderr, "Assertion Failed: %s\n", (message));                                                            \
      fprintf(stderr, "  Condition: %s\n", #condition);                                                                \
      fprintf(stderr, "  File: %s, Line: %d\n", __FILE__, __LINE__);                                                   \
      abort();                                                                                                         \
    }                                                                                                                  \
  } while (0)

/*
 * 为了可移植地处理对齐内存分配：
 * - Windows: _aligned_malloc / _aligned_free
 * - POSIX: posix_memalign / free
 */
#if defined(_WIN32)
#include <malloc.h>
static void *
aligned_malloc_internal(size_t alignment, size_t size)
{
  return _aligned_malloc(size, alignment);
}
static void
aligned_free_internal(void *ptr)
{
  _aligned_free(ptr);
}
#else
// 假定为 POSIX
static void *
aligned_malloc_internal(size_t alignment, size_t size)
{
  void *ptr = NULL;
  if (posix_memalign(&ptr, alignment, size) != 0)
  {
    return NULL;
  }
  return ptr;
}
static void
aligned_free_internal(void *ptr)
{
  free(ptr);
}
#endif

/*
 * --- 对齐和常量 ---
 */

// 检查是否为 2 的幂
static bool
is_power_of_two(size_t n)
{
  return (n != 0) && ((n & (n - 1)) == 0);
}

// 向上取整到 'divisor' (必须是 2 的幂)
static size_t
round_up_to(size_t n, size_t divisor)
{
  assert(is_power_of_two(divisor));
  return (n + divisor - 1) & ~(divisor - 1);
}

// 向下取整到 'divisor' (必须是 2 的幂)
static uintptr_t
round_down_to(uintptr_t n, size_t divisor)
{
  assert(is_power_of_two(divisor));
  return n & ~(divisor - 1);
}

// 对应 `bumpalo` 的 `CHUNK_ALIGN`，它用于确保 Footer 自身是对齐的
#define CHUNK_ALIGN 16
// 确保 Footer 大小是对齐的
#define FOOTER_SIZE (round_up_to(sizeof(ChunkFooter), CHUNK_ALIGN))
// 默认的第一个 Chunk 的*可用*空间 (约 4KB)
#define DEFAULT_CHUNK_SIZE_WITHOUT_FOOTER (4096 - FOOTER_SIZE)

/*
 * --- 哨兵 (Sentinel) 空 Chunk ---
 */

// 对应 `bumpalo` 的 `EMPTY_CHUNK` 静态变量
static ChunkFooter EMPTY_CHUNK_SINGLETON;
static bool EMPTY_CHUNK_INITIALIZED = false;

static ChunkFooter *
get_empty_chunk()
{
  if (!EMPTY_CHUNK_INITIALIZED)
  {
    // 初始化这个哨兵
    // 它的 data 和 ptr 指向自己，prev 也指向自己
    // 这样计算 (ptr - data) 得到 0 字节容量
    EMPTY_CHUNK_SINGLETON.data = (unsigned char *)&EMPTY_CHUNK_SINGLETON;
    EMPTY_CHUNK_SINGLETON.chunk_size = 0;
    EMPTY_CHUNK_SINGLETON.prev = &EMPTY_CHUNK_SINGLETON;
    EMPTY_CHUNK_SINGLETON.ptr = (unsigned char *)&EMPTY_CHUNK_SINGLETON;
    EMPTY_CHUNK_SINGLETON.allocated_bytes = 0;
    EMPTY_CHUNK_INITIALIZED = true;
  }
  return &EMPTY_CHUNK_SINGLETON;
}

static bool
chunk_is_empty(ChunkFooter *footer)
{
  return footer == get_empty_chunk();
}

/*
 * --- 内部 Chunk 管理 ---
 */

// 释放 chunk 链表
static void
dealloc_chunk_list(ChunkFooter *footer)
{
  while (!chunk_is_empty(footer))
  {
    ChunkFooter *prev = footer->prev;
    // data 是 aligned_malloc_internal 返回的指针
    aligned_free_internal(footer->data);
    footer = prev;
  }
}

// 对应 `bumpalo` 的 `new_chunk`
static ChunkFooter *
new_chunk(Bump *bump, size_t new_size_without_footer, size_t align, ChunkFooter *prev)
{

  // new_size_without_footer 是我们期望的*可用*空间
  // 我们需要将其对齐，以确保 footer 也是对齐的
  new_size_without_footer = round_up_to(new_size_without_footer, CHUNK_ALIGN);

  size_t alloc_size;
  if (__builtin_add_overflow(new_size_without_footer, FOOTER_SIZE, &alloc_size))
  {
    return NULL; // 溢出
  }

  // 确保总分配大小也满足对齐
  alloc_size = round_up_to(alloc_size, align);
  if (alloc_size == 0)
    return NULL;

  unsigned char *data = (unsigned char *)aligned_malloc_internal(align, alloc_size);
  if (!data)
    return NULL; // OOM

  // Footer 放在可用空间的末尾
  ChunkFooter *footer_ptr = (ChunkFooter *)(data + new_size_without_footer);

  footer_ptr->data = data;
  footer_ptr->chunk_size = alloc_size;
  footer_ptr->prev = prev;

  // 累积*可用*空间
  footer_ptr->allocated_bytes = prev->allocated_bytes + new_size_without_footer;

  // 碰撞指针(ptr) 从 Footer 的起始位置开始
  // 并向下对齐到 min_align
  uintptr_t ptr_start = (uintptr_t)footer_ptr;
  footer_ptr->ptr = (unsigned char *)round_down_to(ptr_start, bump->min_align);

  // 确保 ptr 仍然在 [data, footer_ptr] 范围内
  assert(footer_ptr->ptr >= footer_ptr->data);

  return footer_ptr;
}

/*
 * --- 分配慢速路径 (Slow Path) ---
 */

// 对应 `bumpalo` 的 `alloc_layout_slow`
// 当快速路径失败 (当前 chunk 空间不足) 时调用
static void *
alloc_layout_slow(Bump *bump, BumpLayout layout)
{
  ChunkFooter *current_footer = bump->current_chunk_footer;

  // 1. 计算新 Chunk 的大小
  size_t prev_usable_size = 0;
  if (!chunk_is_empty(current_footer))
  {
    prev_usable_size = current_footer->chunk_size - FOOTER_SIZE;
  }

  size_t new_size_without_footer;
  if (__builtin_mul_overflow(prev_usable_size, 2, &new_size_without_footer))
  {
    new_size_without_footer = SIZE_MAX; // 溢出
  }

  if (new_size_without_footer < DEFAULT_CHUNK_SIZE_WITHOUT_FOOTER)
  {
    new_size_without_footer = DEFAULT_CHUNK_SIZE_WITHOUT_FOOTER;
  }

  // 确保新 Chunk 至少能放下请求的分配
  size_t requested_align = (layout.align > bump->min_align) ? layout.align : bump->min_align;
  size_t requested_size = round_up_to(layout.size, requested_align);

  if (new_size_without_footer < requested_size)
  {
    new_size_without_footer = requested_size;
  }

  // 2. 检查分配上限
  if (bump->allocation_limit != SIZE_MAX)
  {
    size_t allocated = current_footer->allocated_bytes;
    size_t limit = bump->allocation_limit;
    size_t remaining = (limit > allocated) ? (limit - allocated) : 0;

    if (new_size_without_footer > remaining)
    {
      // 超过上限了
      if (requested_size > remaining)
      {
        return NULL; // 请求的大小本身就超限
      }
      // 否则，只分配刚好够的大小
      new_size_without_footer = requested_size;
    }
  }

  // 3. 确定新 Chunk 的对齐
  size_t chunk_align = (layout.align > CHUNK_ALIGN) ? layout.align : CHUNK_ALIGN;
  chunk_align = (chunk_align > bump->min_align) ? chunk_align : bump->min_align;

  // 4. 创建新 Chunk
  ChunkFooter *new_footer = new_chunk(bump, new_size_without_footer, chunk_align, current_footer);
  if (!new_footer)
  {
    return NULL; // OOM
  }

  // 5. 切换到新 Chunk
  bump->current_chunk_footer = new_footer;

  // 6. 在新 Chunk 上分配 (快速路径)
  // 这一次 *必须* 成功

  // (这里直接内联实现了 `try_alloc_layout_fast`)

  ChunkFooter *footer = new_footer;
  unsigned char *ptr = footer->ptr;
  unsigned char *start = footer->data;

  unsigned char *result_ptr;
  size_t aligned_size;

  // 断言 `ptr` 是 `min_align` 对齐的
  assert(((uintptr_t)ptr % bump->min_align) == 0);

  if (layout.align <= bump->min_align)
  {
    // Case: <= min_align
    if (__builtin_add_overflow(layout.size, bump->min_align - 1, &aligned_size))
      return NULL;
    aligned_size = aligned_size & ~(bump->min_align - 1);

    size_t capacity = (size_t)(ptr - start);
    assert_print(aligned_size <= capacity, "New chunk too small!");

    result_ptr = ptr - aligned_size;
  }
  else
  {
    // Case: > min_align
    if (__builtin_add_overflow(layout.size, layout.align - 1, &aligned_size))
      return NULL;
    aligned_size = aligned_size & ~(layout.align - 1);

    unsigned char *aligned_ptr_end = (unsigned char *)round_down_to((uintptr_t)ptr, layout.align);

    assert_print(aligned_ptr_end >= start, "New chunk alignment failed!");
    size_t capacity = (size_t)(aligned_ptr_end - start);
    assert_print(aligned_size <= capacity, "New chunk too small!");

    result_ptr = aligned_ptr_end - aligned_size;
  }

  assert(((uintptr_t)result_ptr % layout.align) == 0);
  assert(result_ptr >= start);

  footer->ptr = result_ptr;
  return (void *)result_ptr;
}

/*
 * --- 分配快速路径 (Fast Path) ---
 */

// 对应 `bumpalo` 的 `try_alloc_layout_fast`
static void *
try_alloc_layout_fast(Bump *bump, BumpLayout layout)
{
  ChunkFooter *footer = bump->current_chunk_footer;
  unsigned char *ptr = footer->ptr;
  unsigned char *start = footer->data;
  size_t min_align = bump->min_align;

  unsigned char *result_ptr;
  size_t aligned_size;

  // `bumpalo` 保证 ptr 始终是 min_align 对齐的
  assert_print((chunk_is_empty(footer) || ((uintptr_t)ptr % min_align) == 0), "Bump pointer invariant broken");

  if (layout.align <= min_align)
  {
    // Case: <= min_align
    if (__builtin_add_overflow(layout.size, min_align - 1, &aligned_size))
      return NULL;
    aligned_size = aligned_size & ~(min_align - 1);

    size_t capacity = (size_t)(ptr - start);
    if (aligned_size > capacity)
      return NULL; // 空间不足

    result_ptr = ptr - aligned_size;
  }
  else
  {
    // Case: > min_align
    if (__builtin_add_overflow(layout.size, layout.align - 1, &aligned_size))
      return NULL;
    aligned_size = aligned_size & ~(layout.align - 1);

    unsigned char *aligned_ptr_end = (unsigned char *)round_down_to((uintptr_t)ptr, layout.align);

    if (aligned_ptr_end < start)
      return NULL; // 对齐后空间不足

    size_t capacity = (size_t)(aligned_ptr_end - start);
    if (aligned_size > capacity)
      return NULL; // 空间不足

    result_ptr = aligned_ptr_end - aligned_size;
  }

  // 提交
  footer->ptr = result_ptr;
  return (void *)result_ptr;
}

/*
 * ========================================
 * --- 公共 API 实现 ---
 * ========================================
 */

/*
 * --- 生命周期 ---
 */

void
bump_init_with_min_align(Bump *bump, size_t min_align)
{
  assert_print(bump != NULL, "Bump pointer cannot be NULL");
  assert_print(is_power_of_two(min_align), "min_align must be a power of two");
  assert_print(min_align <= CHUNK_ALIGN, "min_align cannot be larger than CHUNK_ALIGN (16)");

  bump->current_chunk_footer = get_empty_chunk();
  bump->allocation_limit = SIZE_MAX;
  bump->min_align = min_align;
}

void
bump_init(Bump *bump)
{
  bump_init_with_min_align(bump, 1);
}

Bump *
bump_new_with_min_align(size_t min_align)
{
  Bump *bump = (Bump *)malloc(sizeof(Bump));
  if (!bump)
    return NULL;
  bump_init_with_min_align(bump, min_align);
  return bump;
}

Bump *
bump_new(void)
{
  return bump_new_with_min_align(1);
}

void
bump_destroy(Bump *bump)
{
  if (bump)
  {
    dealloc_chunk_list(bump->current_chunk_footer);
    // 重置为哨兵，防止二次释放
    bump->current_chunk_footer = get_empty_chunk();
  }
}

void
bump_free(Bump *bump)
{
  if (bump)
  {
    bump_destroy(bump);
    free(bump);
  }
}

void
bump_reset(Bump *bump)
{
  ChunkFooter *current_footer = bump->current_chunk_footer;
  if (chunk_is_empty(current_footer))
  {
    return;
  }

  // 释放所有之前的 Chunks
  dealloc_chunk_list(current_footer->prev);

  // 重置当前 Chunk
  current_footer->prev = get_empty_chunk();

  // 重置碰撞指针 (ptr)
  uintptr_t ptr_start = (uintptr_t)current_footer;
  current_footer->ptr = (unsigned char *)round_down_to(ptr_start, bump->min_align);

  // 重置累计分配大小
  size_t usable_size = (size_t)((unsigned char *)current_footer - current_footer->data);
  current_footer->allocated_bytes = usable_size;
}

/*
 * --- 分配 API ---
 */

void *
bump_alloc_layout(Bump *bump, BumpLayout layout)
{
  // ZST (零大小类型) 或无效对齐
  if (layout.size == 0)
  {
    // 返回一个对齐的、悬垂的指针
    uintptr_t ptr = (uintptr_t)bump->current_chunk_footer->ptr;
    return (void *)round_down_to(ptr, layout.align);
  }
  if (layout.align == 0 || !is_power_of_two(layout.align))
  {
    // C 语言中，_Alignof(char[0]) 可能是 0
    layout.align = 1;
  }

  // 尝试快速路径
  void *alloc = try_alloc_layout_fast(bump, layout);
  if (alloc)
  {
    return alloc;
  }

  // 快速路径失败，进入慢速路径 (分配新 chunk)
  return alloc_layout_slow(bump, layout);
}

void *
bump_alloc(Bump *bump, size_t size, size_t align)
{
  BumpLayout layout = {size, align};
  return bump_alloc_layout(bump, layout);
}

void *
bump_alloc_copy(Bump *bump, const void *src, size_t size, size_t align)
{
  if (size == 0)
  {
    // 行为与 bump_alloc_layout 一致
    BumpLayout layout = {size, align};
    return bump_alloc_layout(bump, layout);
  }

  void *dest = bump_alloc(bump, size, align);
  if (dest && src)
  {
    memcpy(dest, src, size);
  }
  return dest;
}

char *
bump_alloc_str(Bump *bump, const char *str)
{
  if (!str)
    return NULL;

  size_t len = strlen(str);
  size_t size_with_null = len + 1;

  char *dest = (char *)bump_alloc(bump, size_with_null, __alignof(char));
  if (dest)
  {
    memcpy(dest, str, size_with_null);
  }
  return dest;
}

void *
bump_realloc(Bump *bump, void *old_ptr, size_t old_size, size_t new_size, size_t align)
{
  // 1. 如果 old_ptr 为 NULL，等同于 alloc
  if (old_ptr == NULL)
  {
    return bump_alloc(bump, new_size, align);
  }

  // 2. 如果 new_size 为 0，等同于 free (在 arena 中我们什么都不做，返回 NULL)
  if (new_size == 0)
  {
    // realloc(ptr, 0) 的行为是实现定义的。
    // 我们可以返回 NULL (像 free 一样) 或一个有效的 ZST 指针。
    // 返回 NULL 更符合 "free" 的语义。
    return NULL;
  }

  // 3. [!!] 关键: 分配新内存块 [!!]
  // (这是 realloc 的 "慢速路径"，在 arena 中这通常是*唯一*路径)
  void *new_ptr = bump_alloc(bump, new_size, align);
  if (new_ptr == NULL)
  {
    return NULL; // OOM (内存溢出)
  }

  // 4. 复制旧数据
  // 我们只复制 old_size 和 new_size 中较小的那个
  size_t copy_size = (old_size < new_size) ? old_size : new_size;
  if (copy_size > 0)
  {
    memcpy(new_ptr, old_ptr, copy_size);
  }

  // 5. 返回新指针 (旧指针被 "泄露" 在 arena 中)
  return new_ptr;
}

/*
 * --- 容量和限制 ---
 */

void
bump_set_allocation_limit(Bump *bump, size_t limit)
{
  bump->allocation_limit = limit;
}

size_t
bump_get_allocated_bytes(Bump *bump)
{
  return bump->current_chunk_footer->allocated_bytes;
}
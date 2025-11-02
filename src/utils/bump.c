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
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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


static bool
is_power_of_two(size_t n)
{
  return (n != 0) && ((n & (n - 1)) == 0);
}


static size_t
round_up_to(size_t n, size_t divisor)
{
  assert(is_power_of_two(divisor));
  return (n + divisor - 1) & ~(divisor - 1);
}


static uintptr_t
round_down_to(uintptr_t n, size_t divisor)
{
  assert(is_power_of_two(divisor));
  return n & ~(divisor - 1);
}


#define CHUNK_ALIGN 16

#define FOOTER_SIZE (round_up_to(sizeof(ChunkFooter), CHUNK_ALIGN))

#define DEFAULT_CHUNK_SIZE_WITHOUT_FOOTER (4096 - FOOTER_SIZE)

/*
 * --- 哨兵 (Sentinel) 空 Chunk ---
 */


static ChunkFooter EMPTY_CHUNK_SINGLETON;
static bool EMPTY_CHUNK_INITIALIZED = false;

static ChunkFooter *
get_empty_chunk()
{
  if (!EMPTY_CHUNK_INITIALIZED)
  {



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


static void
dealloc_chunk_list(ChunkFooter *footer)
{
  while (!chunk_is_empty(footer))
  {
    ChunkFooter *prev = footer->prev;

    aligned_free_internal(footer->data);
    footer = prev;
  }
}


static ChunkFooter *
new_chunk(Bump *bump, size_t new_size_without_footer, size_t align, ChunkFooter *prev)
{



  new_size_without_footer = round_up_to(new_size_without_footer, CHUNK_ALIGN);

  size_t alloc_size;
  if (__builtin_add_overflow(new_size_without_footer, FOOTER_SIZE, &alloc_size))
  {
    return NULL;
  }


  alloc_size = round_up_to(alloc_size, align);
  if (alloc_size == 0)
    return NULL;

  unsigned char *data = (unsigned char *)aligned_malloc_internal(align, alloc_size);
  if (!data)
    return NULL;


  ChunkFooter *footer_ptr = (ChunkFooter *)(data + new_size_without_footer);

  footer_ptr->data = data;
  footer_ptr->chunk_size = alloc_size;
  footer_ptr->prev = prev;


  footer_ptr->allocated_bytes = prev->allocated_bytes + new_size_without_footer;



  uintptr_t ptr_start = (uintptr_t)footer_ptr;
  footer_ptr->ptr = (unsigned char *)round_down_to(ptr_start, bump->min_align);


  assert(footer_ptr->ptr >= footer_ptr->data);

  return footer_ptr;
}

/*
 * --- 分配慢速路径 (Slow Path) ---
 */



static void *
alloc_layout_slow(Bump *bump, BumpLayout layout)
{
  ChunkFooter *current_footer = bump->current_chunk_footer;


  size_t prev_usable_size = 0;
  if (!chunk_is_empty(current_footer))
  {
    prev_usable_size = current_footer->chunk_size - FOOTER_SIZE;
  }

  size_t new_size_without_footer;
  if (__builtin_mul_overflow(prev_usable_size, 2, &new_size_without_footer))
  {
    new_size_without_footer = SIZE_MAX;
  }

  if (new_size_without_footer < DEFAULT_CHUNK_SIZE_WITHOUT_FOOTER)
  {
    new_size_without_footer = DEFAULT_CHUNK_SIZE_WITHOUT_FOOTER;
  }


  size_t requested_align = (layout.align > bump->min_align) ? layout.align : bump->min_align;
  size_t requested_size = round_up_to(layout.size, requested_align);

  if (new_size_without_footer < requested_size)
  {
    new_size_without_footer = requested_size;
  }


  if (bump->allocation_limit != SIZE_MAX)
  {
    size_t allocated = current_footer->allocated_bytes;
    size_t limit = bump->allocation_limit;
    size_t remaining = (limit > allocated) ? (limit - allocated) : 0;

    if (new_size_without_footer > remaining)
    {

      if (requested_size > remaining)
      {
        return NULL;
      }

      new_size_without_footer = requested_size;
    }
  }


  size_t chunk_align = (layout.align > CHUNK_ALIGN) ? layout.align : CHUNK_ALIGN;
  chunk_align = (chunk_align > bump->min_align) ? chunk_align : bump->min_align;


  ChunkFooter *new_footer = new_chunk(bump, new_size_without_footer, chunk_align, current_footer);
  if (!new_footer)
  {
    return NULL;
  }


  bump->current_chunk_footer = new_footer;






  ChunkFooter *footer = new_footer;
  unsigned char *ptr = footer->ptr;
  unsigned char *start = footer->data;

  unsigned char *result_ptr;
  size_t aligned_size;


  assert(((uintptr_t)ptr % bump->min_align) == 0);

  if (layout.align <= bump->min_align)
  {

    if (__builtin_add_overflow(layout.size, bump->min_align - 1, &aligned_size))
      return NULL;
    aligned_size = aligned_size & ~(bump->min_align - 1);

    size_t capacity = (size_t)(ptr - start);
    assert_print(aligned_size <= capacity, "New chunk too small!");

    result_ptr = ptr - aligned_size;
  }
  else
  {

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


static void *
try_alloc_layout_fast(Bump *bump, BumpLayout layout)
{
  ChunkFooter *footer = bump->current_chunk_footer;
  unsigned char *ptr = footer->ptr;
  unsigned char *start = footer->data;
  size_t min_align = bump->min_align;

  unsigned char *result_ptr;
  size_t aligned_size;


  assert_print((chunk_is_empty(footer) || ((uintptr_t)ptr % min_align) == 0), "Bump pointer invariant broken");

  if (layout.align <= min_align)
  {

    if (__builtin_add_overflow(layout.size, min_align - 1, &aligned_size))
      return NULL;
    aligned_size = aligned_size & ~(min_align - 1);

    size_t capacity = (size_t)(ptr - start);
    if (aligned_size > capacity)
      return NULL;

    result_ptr = ptr - aligned_size;
  }
  else
  {

    if (__builtin_add_overflow(layout.size, layout.align - 1, &aligned_size))
      return NULL;
    aligned_size = aligned_size & ~(layout.align - 1);

    unsigned char *aligned_ptr_end = (unsigned char *)round_down_to((uintptr_t)ptr, layout.align);

    if (aligned_ptr_end < start)
      return NULL;

    size_t capacity = (size_t)(aligned_ptr_end - start);
    if (aligned_size > capacity)
      return NULL;

    result_ptr = aligned_ptr_end - aligned_size;
  }


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


  dealloc_chunk_list(current_footer->prev);


  current_footer->prev = get_empty_chunk();


  uintptr_t ptr_start = (uintptr_t)current_footer;
  current_footer->ptr = (unsigned char *)round_down_to(ptr_start, bump->min_align);


  size_t usable_size = (size_t)((unsigned char *)current_footer - current_footer->data);
  current_footer->allocated_bytes = usable_size;
}

/*
 * --- 分配 API ---
 */

void *
bump_alloc_layout(Bump *bump, BumpLayout layout)
{

  if (layout.size == 0)
  {

    uintptr_t ptr = (uintptr_t)bump->current_chunk_footer->ptr;
    return (void *)round_down_to(ptr, layout.align);
  }
  if (layout.align == 0 || !is_power_of_two(layout.align))
  {

    layout.align = 1;
  }


  void *alloc = try_alloc_layout_fast(bump, layout);
  if (alloc)
  {
    return alloc;
  }


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

  if (old_ptr == NULL)
  {
    return bump_alloc(bump, new_size, align);
  }


  if (new_size == 0)
  {



    return NULL;
  }



  void *new_ptr = bump_alloc(bump, new_size, align);
  if (new_ptr == NULL)
  {
    return NULL;
  }



  size_t copy_size = (old_size < new_size) ? old_size : new_size;
  if (copy_size > 0)
  {
    memcpy(new_ptr, old_ptr, copy_size);
  }


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
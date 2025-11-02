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


#include "utils/string_buf.h"
#include <stdarg.h> // for va_start, va_end, va_copy
#include <stdio.h>  // for vsnprintf
#include <string.h> // for memcpy, strlen

// 缓冲区第一次分配时的默认容量
#define STRING_BUF_INITIAL_CAPACITY 64

/**
 * @brief 内部辅助函数：确保缓冲区至少有 'additional_len' 的额外空间。
 * 如果没有，它将使用 bump_realloc 进行 "分配-复制-丢弃"。
 */
static void
string_buf_ensure_capacity(StringBuf *buf, size_t additional_len)
{
  // 需要的总容量：当前长度 + 额外长度 + 1 (用于 '\0')
  size_t needed_cap = buf->len + additional_len + 1;

  if (needed_cap <= buf->capacity)
  {
    // 空间足够
    return;
  }

  // --- 空间不足，需要增长 ---
  size_t new_cap = (buf->capacity == 0) ? STRING_BUF_INITIAL_CAPACITY : buf->capacity * 2;
  if (new_cap < needed_cap)
  {
    new_cap = needed_cap;
  }

  // [!!] 关键：使用我们新的 bump_realloc [!!]
  // 我们告诉它复制 buf->len 字节，并分配 new_cap 字节
  char *new_data = (char *)bump_realloc(buf->arena,
                                        buf->data, // 旧指针
                                        buf->len,  // [!!] 要复制的旧*数据*大小
                                        new_cap,   // [!!] 要分配的新*总*大小
                                        __alignof(char));

  if (new_data == NULL)
  {
    // OOM (内存溢出)
    buf->data = NULL;
    buf->capacity = 0;
    buf->len = 0;
    return;
  }

  // 更新 buf
  buf->data = new_data;
  buf->capacity = new_cap;
  // 确保新数据是 '\0' 结尾的
  // (realloc 已经复制了 buf->len 字节，所以我们只在末尾写入 '\0')
  buf->data[buf->len] = '\0';
}

// --- 公共 API 实现 ---

void
string_buf_init(StringBuf *buf, Bump *arena)
{
  buf->arena = arena;
  buf->len = 0;
  buf->capacity = 0;
  buf->data = NULL; // 懒分配
}

void
string_buf_destroy(StringBuf *buf)
{
  // 无操作 (No-op)。Bump arena 会处理所有内存的释放。
  (void)buf;
}

void
string_buf_append_bytes(StringBuf *buf, const char *data, size_t len)
{
  if (len == 0)
    return;

  // 1. 确保我们有足够的空间 (包括 '\0')
  string_buf_ensure_capacity(buf, len);
  if (buf->data == NULL)
  {
    return; // OOM 发生
  }

  // 2. 复制新数据
  memcpy(buf->data + buf->len, data, len);
  buf->len += len;

  // 3. 始终保持 '\0' 结尾
  buf->data[buf->len] = '\0';
}

void
string_buf_append_str(StringBuf *buf, const char *str)
{
  string_buf_append_bytes(buf, str, strlen(str));
}

void
string_buf_vappend_fmt(StringBuf *buf, const char *fmt, va_list args)
{
  va_list args_copy;
  va_copy(args_copy, args); // 拷贝 va_list 以防需要两次调用

  // 确保至少有一个初始缓冲区
  if (buf->data == NULL)
  {
    string_buf_ensure_capacity(buf, 0); // 分配初始缓冲区
    if (buf->data == NULL)
    {
      va_end(args_copy);
      return; // OOM
    }
  }

  size_t remaining_cap = buf->capacity - buf->len;

  // 尝试 1: 打印到现有空间
  int n = vsnprintf(buf->data + buf->len, remaining_cap, fmt, args);

  if (n < 0)
  {
    // 编码错误
    va_end(args_copy);
    return;
  }

  size_t bytes_needed = (size_t)n;

  if (bytes_needed < remaining_cap)
  {
    // --- 成功！它装下了 ---
    buf->len += bytes_needed;
    va_end(args_copy);
    return;
  }

  // --- 失败：空间不足 ---
  // 1. 增长缓冲区
  string_buf_ensure_capacity(buf, bytes_needed);
  if (buf->data == NULL)
  {
    va_end(args_copy);
    return; // OOM
  }

  // 2. 再次尝试 (使用 args_copy)
  remaining_cap = buf->capacity - buf->len;
  n = vsnprintf(buf->data + buf->len, remaining_cap, fmt, args_copy);
  va_end(args_copy); // 释放副本

  if (n < 0 || (size_t)n >= remaining_cap)
  {
    return; // OOM 或逻辑错误
  }

  // --- 成功 (第二次尝试) ---
  buf->len += (size_t)n;
}

void
string_buf_append_fmt(StringBuf *buf, const char *fmt, ...)
{
  va_list args;
  va_start(args, fmt);
  string_buf_vappend_fmt(buf, fmt, args); // 调用核心实现
  va_end(args);
}

const char *
string_buf_get(StringBuf *buf)
{
  if (buf->data == NULL)
  {
    // 如果从未附加过任何内容，分配并返回一个空字符串 ""
    char *empty_str = BUMP_ALLOC_SLICE(buf->arena, char, 1);
    if (empty_str)
    {
      empty_str[0] = '\0';
    }
    // (如果 OOM，返回 NULL)
    return empty_str;
  }

  // 缓冲区已由 append* 函数保证是 '\0' 结尾的
  return buf->data;
}
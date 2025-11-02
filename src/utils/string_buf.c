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
#include <stdarg.h>
#include <stdio.h>
#include <string.h>


#define STRING_BUF_INITIAL_CAPACITY 64

/**
 * @brief 内部辅助函数：确保缓冲区至少有 'additional_len' 的额外空间。
 * 如果没有，它将使用 bump_realloc 进行 "分配-复制-丢弃"。
 */
static void
string_buf_ensure_capacity(StringBuf *buf, size_t additional_len)
{

  size_t needed_cap = buf->len + additional_len + 1;

  if (needed_cap <= buf->capacity)
  {

    return;
  }


  size_t new_cap = (buf->capacity == 0) ? STRING_BUF_INITIAL_CAPACITY : buf->capacity * 2;
  if (new_cap < needed_cap)
  {
    new_cap = needed_cap;
  }



  char *new_data = (char *)bump_realloc(buf->arena,
                                        buf->data,
                                        buf->len,
                                        new_cap,
                                        __alignof(char));

  if (new_data == NULL)
  {

    buf->data = NULL;
    buf->capacity = 0;
    buf->len = 0;
    return;
  }


  buf->data = new_data;
  buf->capacity = new_cap;


  buf->data[buf->len] = '\0';
}



void
string_buf_init(StringBuf *buf, Bump *arena)
{
  buf->arena = arena;
  buf->len = 0;
  buf->capacity = 0;
  buf->data = NULL;
}

void
string_buf_destroy(StringBuf *buf)
{

  (void)buf;
}

void
string_buf_append_bytes(StringBuf *buf, const char *data, size_t len)
{
  if (len == 0)
    return;


  string_buf_ensure_capacity(buf, len);
  if (buf->data == NULL)
  {
    return;
  }


  memcpy(buf->data + buf->len, data, len);
  buf->len += len;


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
  va_copy(args_copy, args);


  if (buf->data == NULL)
  {
    string_buf_ensure_capacity(buf, 0);
    if (buf->data == NULL)
    {
      va_end(args_copy);
      return;
    }
  }

  size_t remaining_cap = buf->capacity - buf->len;


  int n = vsnprintf(buf->data + buf->len, remaining_cap, fmt, args);

  if (n < 0)
  {

    va_end(args_copy);
    return;
  }

  size_t bytes_needed = (size_t)n;

  if (bytes_needed < remaining_cap)
  {

    buf->len += bytes_needed;
    va_end(args_copy);
    return;
  }



  string_buf_ensure_capacity(buf, bytes_needed);
  if (buf->data == NULL)
  {
    va_end(args_copy);
    return;
  }


  remaining_cap = buf->capacity - buf->len;
  n = vsnprintf(buf->data + buf->len, remaining_cap, fmt, args_copy);
  va_end(args_copy);

  if (n < 0 || (size_t)n >= remaining_cap)
  {
    return;
  }


  buf->len += (size_t)n;
}

void
string_buf_append_fmt(StringBuf *buf, const char *fmt, ...)
{
  va_list args;
  va_start(args, fmt);
  string_buf_vappend_fmt(buf, fmt, args);
  va_end(args);
}

const char *
string_buf_get(StringBuf *buf)
{
  if (buf->data == NULL)
  {

    char *empty_str = BUMP_ALLOC_SLICE(buf->arena, char, 1);
    if (empty_str)
    {
      empty_str[0] = '\0';
    }

    return empty_str;
  }


  return buf->data;
}
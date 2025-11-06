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
#include <stdarg.h>
#include <stddef.h>

typedef struct
{
  char *data;
  size_t len;
  size_t capacity;
  Bump *arena;
} StringBuf;

void string_buf_init(StringBuf *buf, Bump *arena);
void string_buf_destroy(StringBuf *buf);
void string_buf_append_str(StringBuf *buf, const char *str);
void string_buf_append_bytes(StringBuf *buf, const char *data, size_t len);
/**
 * @brief 附加格式化的字符串 (va_list 版本)。
 *
 * @param buf 字符串缓冲区。
 * @param fmt printf 格式字符串。
 * @param args 已初始化的 va_list。
 */
void string_buf_vappend_fmt(StringBuf *buf, const char *fmt, va_list args);
void string_buf_append_fmt(StringBuf *buf, const char *fmt, ...);
const char *string_buf_get(StringBuf *buf);

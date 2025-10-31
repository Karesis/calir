#ifndef CALIR_STRING_BUF_H
#define CALIR_STRING_BUF_H

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

#endif // CALIR_STRING_BUF_H
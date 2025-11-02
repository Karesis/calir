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

#include "ir/printer.h"
#include <stdio.h>

/*
 * --- 机制 1: FILE* 实现 ---
 */
static void
ir_printer_file_append_str(void *target, const char *str)
{
  fprintf((FILE *)target, "%s", str);
}
static void
ir_printer_file_append_vfmt(void *target, const char *fmt, va_list args)
{
  vfprintf((FILE *)target, fmt, args);
}

/*
 * --- 机制 2: StringBuf* 实现 ---
 */
static void
ir_printer_string_buf_append_str(void *target, const char *str)
{
  string_buf_append_str((StringBuf *)target, str);
}
static void
ir_printer_string_buf_append_vfmt(void *target, const char *fmt, va_list args)
{

  string_buf_vappend_fmt((StringBuf *)target, fmt, args);
}

/*
 * --- 公共策略 API ---
 */

void
ir_printer_init_file(IRPrinter *p, FILE *f)
{
  p->target = f;
  p->append_str_func = ir_printer_file_append_str;
  p->append_vfmt_func = ir_printer_file_append_vfmt;
}

void
ir_printer_init_string_buf(IRPrinter *p, StringBuf *buf)
{
  p->target = buf;
  p->append_str_func = ir_printer_string_buf_append_str;
  p->append_vfmt_func = ir_printer_string_buf_append_vfmt;
}

/*
 * --- 公共机制 API ---
 */

void
ir_print_str(IRPrinter *p, const char *str)
{
  p->append_str_func(p->target, str);
}

void
ir_printf(IRPrinter *p, const char *fmt, ...)
{
  va_list args;
  va_start(args, fmt);
  p->append_vfmt_func(p->target, fmt, args);
  va_end(args);
}
#ifndef CALIR_IR_PRINTER_H
#define CALIR_IR_PRINTER_H

#include "utils/string_buf.h"
#include <stdarg.h>
#include <stdio.h>

/**
 * @brief IR 打印机 (机制)。
 * 这是一个抽象，用于将所有 ir_..._dump 函数与
 * 它们的输出目标（策略）分离。
 */
typedef struct IRPrinter
{
  // 策略：目标对象 (FILE* 或 StringBuf*)
  void *target;
  // 机制 1：附加一个原始字符串
  void (*append_str_func)(void *target, const char *str);
  // 机制 2：附加一个格式化的 va_list
  void (*append_vfmt_func)(void *target, const char *fmt, va_list args);
} IRPrinter;

/*
 * --- 策略 API ---
 */

/**
 * @brief 策略 1: 初始化打印机以写入 FILE*
 */
void ir_printer_init_file(IRPrinter *p, FILE *f);

/**
 * @brief 策略 2: 初始化打印机以写入 StringBuf*
 */
void ir_printer_init_string_buf(IRPrinter *p, StringBuf *buf);

/*
 * --- 机制 API (供 dump 函数使用) ---
 */

/**
 * @brief 机制：附加一个原始 C 字符串。
 * (等同于 fprintf(f, "%s", str))
 */
void ir_print_str(IRPrinter *p, const char *str);

/**
 * @brief 机制：附加一个格式化字符串。
 * (等同于 fprintf(f, "...", ...))
 */
void ir_printf(IRPrinter *p, const char *fmt, ...);

#endif // CALIR_IR_PRINTER_H
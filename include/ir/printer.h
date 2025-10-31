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

/*
 * --- 具体打印函数 ---
 */

/**
 * @brief [策略 1] 将模块的 IR 打印到指定的流 (例如 stdout)
 * (这是旧的 ir_module_dump)
 */
void ir_module_dump_to_file(IRModule *mod, FILE *stream);

/**
 * @brief [策略 2] 将模块的 IR 打印到 arena 上的新字符串
 *
 * @param mod 要打印的模块
 * @param arena 用于分配字符串的 Bump arena
 * @return const char* 指向 arena 上的、以 '\0' 结尾的字符串
 */
const char *ir_module_dump_to_string(IRModule *mod, Bump *arena);

/**
 * @brief [内部机制] 核心 dump 函数。
 * (除非你正在实现一个新的 IRPrinter 策略，否则不应直接调用)
 */
void ir_module_dump_internal(IRModule *mod, IRPrinter *p);

#endif // CALIR_IR_PRINTER_H
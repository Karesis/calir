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

#ifndef CALIR_INTERPRETER_INTERPRETER_H
#define CALIR_INTERPRETER_INTERPRETER_H

#include "ir/function.h"
#include "ir/module.h"
#include "utils/bump.h"
#include "utils/data_layout.h"
#include "utils/hashmap.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief 运行时值的类型标签
 * (这与 IRTypeKind 相似，但用于运行时)
 */
typedef enum RuntimeValueKind
{
  RUNTIME_VAL_UNDEF,
  RUNTIME_VAL_I1,
  RUNTIME_VAL_I8,
  RUNTIME_VAL_I16,
  RUNTIME_VAL_I32,
  RUNTIME_VAL_I64,
  RUNTIME_VAL_F32,
  RUNTIME_VAL_F64,
  RUNTIME_VAL_PTR
} RuntimeValueKind;

/**
 * @brief 解释器中的一个运行时值 (Tagged Union)
 *
 * 这是 interpreter 和调用者之间传递数据的公共结构。
 */
typedef struct RuntimeValue
{
  RuntimeValueKind kind;
  union {
    bool val_i1;
    int8_t val_i8;
    int16_t val_i16;
    int32_t val_i32;
    int64_t val_i64;
    float val_f32;
    double val_f64;
    /**
     * @brief 模拟的指针。
     *
     * 对于 'alloca'，这将指向由 'ExecutionContext' 的 'stack_arena' 分配的内存。
     * 对于 'global'，这将指向由 'Interpreter' 的 'global_memory' (未来) 分配的内存。
     * 对于 'load'/'store'，这必须是一个有效的 void*。
     */
    void *val_ptr;
  } as;
} RuntimeValue;

typedef enum ExecutionResultKind
{
  EXEC_OK,
  EXEC_RUNNING,
  EXEC_ERR_STACK_OVERFLOW,
  EXEC_ERR_DIV_BY_ZERO_S,
  EXEC_ERR_DIV_BY_ZERO_U,
  EXEC_ERR_DIV_BY_ZERO_F,
  EXEC_ERR_INVALID_PTR,
} ExecutionResultKind;

/**
 * @brief 解释器主上下文 (Interpreter Main Context)
 *
 * 这是一个长时对象 (long-lived object)，它持有一个主内存竞技场
 * 用于在多次函数调用期间分配 *持久* 的运行时值。
 *
 * (这与 ExecutionContext 不同，后者是*每次*函数调用时
 * 在 .c 文件内部创建的临时对象)
 */
typedef struct Interpreter
{
  /**
   * @brief 主竞技场 (Master Arena).
   *
   * 用于分配 RuntimeValue 对象 (当它们作为常量或
   * 需要在调用之间共享时)。
   * [!!] 注意：你 .c 文件中的 `bump_new()` / `bump_free()`
   * [!!] 和这个 `Bump *arena` 成员是等价的。
   */
  Bump *arena;

  /**
   * @brief [!!] (新增) 全局变量存储。
   *
   * 这是一个持久映射:
   * Key: IRValueNode* (e.g., &global->value)
   * Value: RuntimeValue* (一个 RUNTIME_VAL_PTR 类型的持久值)
   */
  PtrHashMap *global_storage;

  /**
   * @brief FFI 链接表
   * Map<const char* (函数名), CalicoHostFunction (函数指针)>
   */
  StrHashMap *external_function_map;

  DataLayout *data_layout;

} Interpreter;

typedef struct ExecutionContext
{
  /** @brief 指向父解释器，用于访问持久竞技场 (e.g., 用于常量) */
  Interpreter *interp;

  /** @brief 寄存器堆 (Register File). 映射: IRValueNode* -> RuntimeValue* */
  PtrHashMap *frame;

  /** @brief 临时值竞技场 (Temporary Value Arena)。*/
  Bump value_arena;

  /** @brief 模拟栈 (Stack Arena)。*/
  Bump stack_arena;

  /** * @brief [!!] (重构) 存储运行时错误信息
   * 当辅助函数返回 ERR 时，它们会顺便设置这个。
   */
  const char *error_message;
} ExecutionContext;

/**
 * @brief 所有宿主 FFI 函数必须匹配的 C 函数签名
 *
 * @param ctx (可选) 指向当前执行上下文 (用于在 FFI 中报告错误)
 * @param args (输入) 运行时参数数组
 * @param num_args (输入) 参数数量
 * @param result_out (输出) FFI 函数必须将结果写入这里
 * @return 执行状态 (EXEC_OK 或错误)
 */
typedef ExecutionResultKind (*CalicoHostFunction)(ExecutionContext *ctx, RuntimeValue **args, size_t num_args,
                                                  RuntimeValue *result_out);

/**
 * @brief 创建一个新的解释器实例。
 * @param data_layout [!!] 解释器将 *借用* 的数据布局。
 * @param data_layout 必须由调用者管理生命周期，并且必须
 * @param data_layout 存活时间长于此 Interpreter 实例。
 * @return 指向新 Interpreter 的指针。
 */
Interpreter *interpreter_create(DataLayout *data_layout);

/**
 * @brief 销毁解释器实例并释放其所有内存。
 * @param interp 要销毁的解释器。
 */
void interpreter_destroy(Interpreter *interp);

/**
 * @brief [FFI] 注册一个宿主 C 函数，使其可在 IR 中被调用
 *
 * @param interp 解释器实例
 * @param name IR 中的函数名 (e.g., "printf")
 * @param fn_ptr 指向宿主 C ABI 包装器的函数指针
 */
void interpreter_register_external_function(Interpreter *interp, const char *name, CalicoHostFunction fn_ptr);

/**
 * @brief (公开 API) 运行 (解释) 一个 IR 函数。
 *
 * @param interp [!!] 解释器实例。
 * @param func 要运行的函数。
 * @param args (可选) 传递给函数的参数 (RuntimeValue* 数组)。
 * @param num_args 参数的数量。
 * @param result_out [out] 用于存储函数返回值的指针。
 * @return 如果函数成功执行 (e.g., 遇到 'ret') 则返回 true，
 * 如果发生运行时错误 (e.g., 除以零) 则返回 false。
 */
bool interpreter_run_function(Interpreter *interp, IRFunction *func, RuntimeValue **args, size_t num_args,
                              RuntimeValue *result_out);

#endif

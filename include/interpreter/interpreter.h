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
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief 运行时值的类型标签
 * (这与 IRTypeKind 相似，但用于运行时)
 */
typedef enum
{
  RUNTIME_VAL_UNDEF,
  RUNTIME_VAL_I1,
  RUNTIME_VAL_I8,
  RUNTIME_VAL_I16,
  RUNTIME_VAL_I32,
  RUNTIME_VAL_I64,
  RUNTIME_VAL_F32,
  RUNTIME_VAL_F64,
  RUNTIME_VAL_PTR // 指向 *宿主* (host) 内存的指针
} RuntimeValueKind;

/**
 * @brief 解释器中的一个运行时值 (Tagged Union)
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
    void *val_ptr; // 用于 alloca, GEP 等
  } as;
} RuntimeValue;

/**
 * @brief 解释器上下文
 *
 * 持有所有运行时分配 (RuntimeValue, ExecutionFrame) 的内存。
 */
typedef struct Interpreter
{
  Bump *arena; // 用于所有运行时对象的竞技场
} Interpreter;

// --- 解释器 API ---

/**
 * @brief 创建一个新的解释器实例。
 * @return 指向新 Interpreter 的指针。
 */
Interpreter *interpreter_create(void);

/**
 * @brief 销毁解释器实例并释放其所有内存。
 * @param interp 要销毁的解释器。
 */
void interpreter_destroy(Interpreter *interp);

/**
 * @brief 运行 (解释) 一个 IR 函数。
 *
 * @param interp 解释器实例。
 * @param func 要运行的函数。
 * @param args (可选) 传递给函数的参数 (RuntimeValue* 数组)。
 * @param num_args 参数的数量。
 * @param result_out [out] 用于存储函数返回值的指针。
 * @return 如果函数成功执行 (e.g., 遇到 'ret') 则返回 true，
 * 如果发生运行时错误 (e.g., 除以零) 则返回 false。
 */
bool interpreter_run_function(Interpreter *interp, IRFunction *func, RuntimeValue **args, size_t num_args,
                              RuntimeValue *result_out);

#endif // CALIR_INTERPRETER_INTERPRETER_H
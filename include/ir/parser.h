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


/* include/ir/parser.h */
#ifndef IR_PARSER_H
#define IR_PARSER_H

#include "ir/builder.h"    // For IRBuilder
#include "ir/context.h"    // For IRContext
#include "ir/function.h"   // For IRFunction
#include "ir/lexer.h"      // For Lexer
#include "ir/module.h"     // For IRModule
#include "utils/bump.h"    // For Bump
#include "utils/hashmap.h" // For PtrHashMap
#include <stdbool.h>       // For bool

/**
 * @brief Parser 状态机
 *
 * 封装了解析文本 IR 所需的所有状态。
 * 它不是一个持久对象；它在 ir_parse_module 函数开始时创建，
 * 在解析完成或失败时销毁。
 */
typedef struct Parser
{
  /** @brief 指向 Lexer 的指针，用于消耗 Token。*/
  Lexer *lexer;

  /** @brief 指向全局 IR 上下文的指针。*/
  IRContext *context;

  /** @brief 指向我们正在构建的目标模块的指针。*/
  IRModule *module;

  /** @brief 指向 IRBuilder 的指针，用于插入指令。*/
  IRBuilder *builder;

  /** @brief 指向当前正在解析的函数 (如果不在函数内部，则为 NULL)。*/
  IRFunction *current_function;

  /**
   * @brief 临时分配器。
   * 用于分配 Parser 的临时数据，例如：
   * 1. 当前函数的 local_value_map。
   * 2. 解析 GEP 或 Call 指令时的临时参数数组。
   *
   * 它在进入新函数时被重置 (bump_reset)。
   */
  Bump temp_arena;

  /**
   * @brief 全局符号表 (值映射)。
   * Map<const char* (interned), IRValueNode*>
   * 存储 @globals 和 @functions。
   * 在 Parser 的生命周期内持续存在 (在 ir_arena 上分配)。
   */
  PtrHashMap *global_value_map;

  /**
   * @brief 局部符号表 (值映射)。
   * Map<const char* (interned), IRValueNode*>
   * 存储 %locals, %args, 和 %labels。
   * 在进入函数时创建 (在 temp_arena 上)，在退出函数时销毁。
   */
  PtrHashMap *local_value_map;

  /** @brief 错误标志。如果解析过程中发生错误，则设置为 true。*/
  bool has_error;

  /**
   * @brief [!! 新增 !!] 存储发生的*第一个*错误的详细信息
   */
  struct
  {
    char message[256]; // 格式化后的错误信息
    size_t line;       // 发生错误的行号
    size_t column;     // 发生错误的列号
  } error;

} Parser;

/**
 * @brief 解析一个完整的 IR 模块
 *
 * 这是 Parser 的主入口点。
 * 它接收一个包含 IR 文本的字符串缓冲区，并返回一个
 * 构建好的 IRModule 对象 (在 Context 的 ir_arena 中)。
 *
 * @param ctx 全局 IR 上下文
 * @param source_buffer 包含要解析的 IR 文本的 C 字符串
 * @return IRModule* 如果解析成功，返回指向新模块的指针。
 * @return NULL 如果解析失败 (例如语法错误)。
 */
IRModule *ir_parse_module(IRContext *ctx, const char *source_buffer);

#endif // IR_PARSER_H
#ifndef CALIR_IR_PARSER_H
#define CALIR_IR_PARSER_H

#include "ir/builder.h"
#include "ir/context.h"
#include "ir/lexer.h"
#include "ir/module.h"
#include "utils/hashmap.h"

// 前向声明
typedef struct IRContext IRContext;
typedef struct IRModule IRModule;
typedef struct IRFunction IRFunction;
typedef struct IRBasicBlock IRBasicBlock;
typedef struct Lexer Lexer;
typedef struct IRBuilder IRBuilder;
typedef struct PtrHashMap PtrHashMap;

/**
 * @brief IR 解析器
 *
 * 持有解析一个模块所需的全部状态。
 */
typedef struct Parser
{
  Lexer *lexer;                 // 词法分析器 (Token 源)
  IRContext *context;           // IR 上下文 (用于创建 Types/Constants)
  IRBuilder *builder;           // IR 构建器 (用于创建 Instructions)
  IRModule *module;             // 正在构建的模块
  IRFunction *current_function; // 正在解析的当前函数

  /**
   * @brief [核心] 符号表 (Symbol Table)
   *
   * 映射: const char* (来自 Lexer) -> IRValueNode*
   * - "%foo" -> IRInstruction*, IRArgument*
   * - "@bar" -> IRFunction*, IRGlobalVariable*
   */
  PtrHashMap *value_map;

  /**
   * @brief 基本块映射 (用于前向引用)
   * * 映射: const char* (label name) -> IRBasicBlock*
   * 允许 'br label %later' 在 '%later:' 定义之前出现。
   */
  PtrHashMap *bb_map;

} Parser;

// --- Parser API ---

/**
 * @brief 创建一个新的 Parser 实例
 * @param ctx IR 上下文
 * @param mod 要填充的 IR 模块
 * @return Parser* (必须由 ir_parser_destroy 释放)
 */
Parser *ir_parser_create(IRContext *ctx, IRModule *mod);

/**
 * @brief 销毁 Parser 实例
 * (注意: 不会销毁 Context 或 Module)
 * @param parser 要销毁的 Parser
 */
void ir_parser_destroy(Parser *parser);

/**
 * @brief [主 API] 解析一个 .cir 文本缓冲区并填充模块
 *
 * @param parser Parser 实例
 * @param buffer 包含 .cir 文本的 C 字符串 (以 '\0' 结尾)
 * @return 如果解析成功返回 true，否则返回 false (并打印错误)
 */
bool ir_parse_buffer(Parser *parser, const char *buffer);

#endif // CALIR_IR_PARSER_H
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

#ifndef CALIR_IR_LEXER_H
#define CALIR_IR_LEXER_H

#include "ir/context.h"
#include <stddef.h>
#include <stdint.h>

typedef struct IRContext IRContext;

/**
 * @brief 词法单元 (Token) 的类型
 */
typedef enum
{
  TK_ILLEGAL, // 非法
  TK_EOF,     // 文件结束

  /// --- 标识符和字面量 ---
  TK_IDENT,        // 普通标识符 (e.g., "i32", "my_struct")
  TK_GLOBAL_IDENT, // @name
  TK_LOCAL_IDENT,  // %name
  TK_LABEL_IDENT,  // $name

  TK_INTEGER_LITERAL,
  TK_FLOAT_LITERAL,
  TK_STRING_LITERAL,

  /// --- 标点符号 ---
  TK_EQ,        // =
  TK_COMMA,     // ,
  TK_COLON,     // :
  TK_LBRACE,    // {
  TK_RBRACE,    // }
  TK_LBRACKET,  // [
  TK_RBRACKET,  // ]
  TK_LPAREN,    // (
  TK_RPAREN,    // )
  TK_LT,        // <
  TK_GT,        // >
  TK_ELLIPSIS,  // ... (用于可变参数)
  TK_SEMICOLON, // ; (用于注释)

  /// --- 顶级关键字 ---
  TK_KW_MODULE,
  TK_KW_DEFINE,
  TK_KW_DECLARE,
  TK_KW_GLOBAL,
  TK_KW_TYPE,

  /// --- 终结者指令 ---
  TK_KW_RET,
  TK_KW_BR,
  TK_KW_COND_BR, // [注意] 你的 parser 可能会决定只用 'br'
  TK_KW_SWITCH,
  TK_KW_DEFAULT, // 用于 switch

  /// --- 二元运算 ---
  TK_KW_ADD,
  TK_KW_SUB,
  TK_KW_MUL,
  TK_KW_UDIV,
  TK_KW_SDIV,
  TK_KW_UREM,
  TK_KW_SREM,
  TK_KW_FADD,
  TK_KW_FSUB,
  TK_KW_FMUL,
  TK_KW_FDIV,
  TK_KW_SHL,
  TK_KW_LSHR,
  TK_KW_ASHR,
  TK_KW_AND,
  TK_KW_OR,
  TK_KW_XOR,

  /// --- 内存和比较 ---
  TK_KW_ALLOCA,
  TK_KW_LOAD,
  TK_KW_STORE,
  TK_KW_GEP,
  TK_KW_INBOUNDS, // 用于 GEP
  TK_KW_ICMP,
  TK_KW_FCMP,

  /// --- 类型转换 ---
  TK_KW_TRUNC,
  TK_KW_ZEXT,
  TK_KW_SEXT,
  TK_KW_FPTRUNC,
  TK_KW_FPEXT,
  TK_KW_FPTOUI,
  TK_KW_FPTOSI,
  TK_KW_UITOFP,
  TK_KW_SITOFP,
  TK_KW_PTRTOINT,
  TK_KW_INTTOPTR,
  TK_KW_BITCAST,
  TK_KW_TO, // 用于转换指令

  /// --- 常量关键字 ---
  TK_KW_UNDEF,
  TK_KW_NULL,
  TK_KW_ZEROINITIALIZER,
  TK_KW_VOID,

  /// --- 其他 ---
  TK_KW_PHI,
  TK_KW_CALL,

  /// ICMP, FCMP 共有谓词
  TK_KW_EQ,  /// eq (ICMP)
  TK_KW_NE,  /// ne (ICMP)
  TK_KW_UGT, /// ugt (ICMP + FCMP)
  TK_KW_UGE, /// uge (ICMP + FCMP)
  TK_KW_ULT, /// ult (ICMP + FCMP)
  TK_KW_ULE, /// ule (ICMP + FCMP)
  TK_KW_SGT, /// sgt (ICMP)
  TK_KW_SGE, /// sge (ICMP)
  TK_KW_SLT, /// slt (ICMP)
  TK_KW_SLE, /// sle (ICMP)

  /// FCMP 独有
  TK_KW_OEQ,
  TK_KW_OGT,
  TK_KW_OGE,
  TK_KW_OLT,
  TK_KW_OLE,
  TK_KW_ONE,
  TK_KW_UEQ,
  /// (UGT, UGE, ULT, ULE 在上面)
  TK_KW_UNE,
  TK_KW_ORD,
  TK_KW_UNO,
  TK_KW_TRUE,
  TK_KW_FALSE,

} TokenType;

/**
 * @brief 词法单元 (Token) 结构体
 *
 * 存储类型和（如果适用）解析好的值。
 */
typedef struct Token
{
  TokenType type;
  size_t line;
  size_t column;

  union {

    const char *ident_val;

    int64_t int_val;

    double float_val;
  } as;
} Token;

/**
 * @brief 词法分析器 (Lexer)
 *
 * 这是一个 "Peeking" Lexer，它总是有一个 'current_token'。
 */
typedef struct Lexer
{
  IRContext *context;
  const char *buffer_start;
  const char *ptr;
  const char *line_start;
  int line;

  Token current;
  Token peek;
} Lexer;

/**
 * @brief 初始化 Lexer
 * @param lexer Lexer 实例
 * @param buffer 包含 .cir 文件内容的 C 字符串 (必须以 '\0' 结尾)
 * @param ctx IR 上下文 (用于字符串驻留)
 */
void ir_lexer_init(Lexer *lexer, const char *buffer, IRContext *ctx);

/**
 * @brief "吃掉" 当前 Token，并让 Lexer 解析下一个 Token。
 *
 * 将 peek_token 移动到 current_token，并解析新的 peek_token。
 * (为了简单起见，我们先实现一个 LL(1) 的，只用 current_token)
 */
void ir_lexer_next(Lexer *lexer);

/**
 * @brief 获取*当前* Token (K=1)
 *
 * @param lexer Lexer 实例
 * @return const Token*
 */
const Token *ir_lexer_current_token(const Lexer *lexer);

/**
 * @brief 预读*下一个* Token (K=2)
 *
 * @param lexer Lexer 实例
 * @return const Token*
 */
const Token *ir_lexer_peek_token(const Lexer *lexer);

/**
 * @brief [辅助] "吃掉" 当前 Token，如果它匹配预期类型。
 * @param lexer Lexer
 * @param expected 预期的 Token 类型
 * @return 如果 Token 匹配则返回 true，否则返回 false (并设置 TK_ILLEGAL)
 */
bool ir_lexer_eat(Lexer *lexer, TokenType expected);

#endif
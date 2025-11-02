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
  TK_ILLEGAL,
  TK_EOF,

  TK_IDENT,
  TK_GLOBAL_IDENT,
  TK_LOCAL_IDENT,
  TK_LABEL_IDENT,

  TK_INTEGER_LITERAL,
  TK_FLOAT_LITERAL,
  TK_STRING_LITERAL,

  TK_EQ,
  TK_COMMA,
  TK_COLON,
  TK_LBRACE,
  TK_RBRACE,
  TK_LBRACKET,
  TK_RBRACKET,
  TK_LPAREN,
  TK_RPAREN,
  TK_LT,
  TK_GT,
  TK_ELLIPSIS,
  TK_SEMICOLON,

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
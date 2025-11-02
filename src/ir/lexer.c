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


/* src/ir/lexer.c */
#include "ir/lexer.h"
#include "ir/context.h"
#include "utils/id_list.h" // 包含 container_of (虽然这里没用，但保持一致)

#include <assert.h>
#include <ctype.h>  // for isalpha, isdigit, isalnum
#include <stdlib.h> // for strtoll
#include <string.h> // for strncmp

// -----------------------------------------------------------------
// 辅助谓词 (Helper Predicates)
// -----------------------------------------------------------------

static bool
is_ident_start(char c)
{
  return isalpha(c) || c == '_';
}

static bool
is_ident_continue(char c)
{
  return isalnum(c) || c == '_' || c == '.';
}

// -----------------------------------------------------------------
// 辅助解析器 (Helper Parsers)
// -----------------------------------------------------------------

static char
current_char(Lexer *l)
{
  return *l->ptr;
}

static char
advance(Lexer *l)
{
  char c = *l->ptr;
  if (c != '\0')
  {
    l->ptr++;
  }
  return c;
}

static char
peek_char(Lexer *l)
{
  if (*l->ptr == '\0')
  {
    return '\0';
  }
  return *(l->ptr + 1);
}

static void
skip_comment(Lexer *l)
{
  while (current_char(l) != '\n' && current_char(l) != '\0')
  {
    advance(l);
  }
}

static void
skip_whitespace(Lexer *l)
{
  while (true)
  {
    char c = current_char(l);
    switch (c)
    {
    case ' ':
    case '\t':
    case '\r':
      advance(l);
      break;
    case '\n':
      advance(l);
      l->line++;
      l->line_start = l->ptr;
      break;
    case ';':
      skip_comment(l);
      break;
    default:
      return;
    }
  }
}

/**
 * @brief [内部] 解析一个 TK_IDENT
 * @param l Lexer
 * @param out_token [输出] 存储结果的 Token
 */
static void
parse_ident(Lexer *l, Token *out_token)
{
  const char *start = l->ptr;
  // (我们已经知道第一个字符是 ident_start)
  advance(l);

  while (is_ident_continue(current_char(l)))
  {
    advance(l);
  }
  size_t len = l->ptr - start;

  out_token->type = TK_IDENT;
  out_token->as.ident_val = ir_context_intern_str_slice(l->context, start, len);
}

/**
 * @brief [内部] 解析 TK_GLOBAL_IDENT 或 TK_LOCAL_IDENT
 * @param l Lexer
 * @param type (TK_GLOBAL_IDENT 或 TK_LOCAL_IDENT)
 * @param out_token [输出] 存储结果的 Token
 */
static void
parse_global_or_local(Lexer *l, TokenType type, Token *out_token)
{

  const char *start = l->ptr;

  if (!is_ident_continue(current_char(l)))
  {
    out_token->type = TK_ILLEGAL;
    return;
  }

  while (is_ident_continue(current_char(l)))
  {
    advance(l);
  }
  size_t len = l->ptr - start;

  out_token->type = type;
  out_token->as.ident_val = ir_context_intern_str_slice(l->context, start, len);
}

/**
 * @brief [内部] 解析 TK_INTEGER_LITERAL 或 TK_FLOAT_LITERAL
 * @param l Lexer
 * @param out_token [输出] 存储结果的 Token
 */
static void
parse_number(Lexer *l, Token *out_token)
{
  bool is_negative = false;

  if (current_char(l) == '-')
  {
    is_negative = true;
    advance(l);
  }

  // (我们已经知道至少有一位数字)
  int64_t int_part = 0;
  while (isdigit(current_char(l)))
  {
    int_part = int_part * 10 + (advance(l) - '0');
  }

  // [新] 检查浮点数
  if (current_char(l) == '.' && isdigit(peek_char(l)))
  {
    // 这是一个浮点数
    advance(l); // 消耗 '.'

    double frac_part = 0.0;
    double div = 10.0;
    while (isdigit(current_char(l)))
    {
      frac_part = frac_part + (advance(l) - '0') / div;
      div *= 10.0;
    }

    out_token->type = TK_FLOAT_LITERAL;
    double final_val = (double)int_part + frac_part;
    out_token->as.float_val = is_negative ? -final_val : final_val;
  }
  else
  {
    // 这是一个整数
    out_token->type = TK_INTEGER_LITERAL;
    out_token->as.int_val = is_negative ? -int_part : int_part;
  }

  // 检查非法后缀 (e.g., "123foo")
  if (is_ident_start(current_char(l)))
  {
    out_token->type = TK_ILLEGAL;
  }
}

/**
 * @brief [内部] 解析 TK_STRING_LITERAL
 * @param l Lexer
 * @param out_token [输出] 存储结果的 Token
 */
static void
parse_string(Lexer *l, Token *out_token)
{
  const char *start = l->ptr;

  // 循环直到找到结束的 '"'
  // TODO: 当前不支持转义字符 (e.g., \" or \n)
  while (current_char(l) != '"' && current_char(l) != '\0')
  {
    advance(l);
  }

  if (current_char(l) == '\0')
  {
    // 未闭合的字符串
    out_token->type = TK_ILLEGAL;
    return;
  }

  size_t len = l->ptr - start;
  advance(l); // 消耗 '"'

  out_token->type = TK_STRING_LITERAL;
  // (我们 intern 字符串的 *内容*，不包括引号)
  out_token->as.ident_val = ir_context_intern_str_slice(l->context, start, len);
}

// -----------------------------------------------------------------
// 核心扫描器 (Core Scanner)
// -----------------------------------------------------------------

/**
 * @brief  扫描下一个 Token 并填充 out_token。
 */
static void
lexer_scan_token(Lexer *l, Token *out_token)
{
  // 1. 跳过空白和注释
  skip_whitespace(l);

  // 2. 存储 Token 的起始行号
  out_token->line = l->line;
  out_token->column = (l->ptr - l->line_start) + 1;

  // 3. 消耗一个字符并进行分派
  char c = advance(l);

  switch (c)
  {
  // --- 文件结束 ---
  case '\0':
    out_token->type = TK_EOF;
    break;

  // --- 标点符号 ---
  case '=':
    out_token->type = TK_EQ;
    break;
  case ',':
    out_token->type = TK_COMMA;
    break;
  case ':':
    out_token->type = TK_COLON;
    break;
  case '{':
    out_token->type = TK_LBRACE;
    break;
  case '}':
    out_token->type = TK_RBRACE;
    break;
  case '[':
    out_token->type = TK_LBRACKET;
    break;
  case ']':
    out_token->type = TK_RBRACKET;
    break;
  case '(':
    out_token->type = TK_LPAREN;
    break;
  case ')':
    out_token->type = TK_RPAREN;
    break;
  case '<':
    out_token->type = TK_LT;
    break;
  case '>':
    out_token->type = TK_GT;
    break;
    // 处理 '...'
  case '.':
    if (current_char(l) == '.' && peek_char(l) == '.')
    {
      advance(l); // 消耗第二个 '.'
      advance(l); // 消耗第三个 '.'
      out_token->type = TK_ELLIPSIS;
    }
    else
    {
      // '.' 或 '..' 都是非法的
      out_token->type = TK_ILLEGAL;
    }
    break;
  // --- 标识符 ---
  case '@':
    parse_global_or_local(l, TK_GLOBAL_IDENT, out_token);
    break;
  case '%':
    parse_global_or_local(l, TK_LOCAL_IDENT, out_token);
    break;
  case '$':
    parse_global_or_local(l, TK_LABEL_IDENT, out_token);
    break;

  // 字符串
  case '"':
    parse_string(l, out_token);
    break;

  // --- 默认情况 (标识符, 数字, 或非法字符) ---
  default:
    // 1. 普通标识符 (e.g., 'define', 'add')
    if (is_ident_start(c))
    {
      l->ptr--; // 回退一步
      parse_ident(l, out_token);
    }
    // 2. 数字 (e.g., '123', '-42', '1.23')
    else if (isdigit(c) || (c == '-' && isdigit(peek_char(l))))
    {
      // parse_number 仍然会处理 '-' 和 '.'
      // 但我们只在 '-' 后面*明确*跟着数字时才进入
      l->ptr--; // 回退一步
      parse_number(l, out_token);
    }
    // 3. 非法字符
    else
    {
      out_token->type = TK_ILLEGAL;
    }
    break;
  }
}

// -----------------------------------------------------------------
// 公共 API (Public API) - [!! 已升级 !!]
// -----------------------------------------------------------------

/**
 * @brief 初始化 Lexer
 */
void
ir_lexer_init(Lexer *lexer, const char *buffer, IRContext *ctx)
{
  assert(lexer && buffer && ctx);
  lexer->context = ctx;
  lexer->buffer_start = buffer;
  lexer->ptr = buffer;
  lexer->line = 1;
  lexer->line_start = buffer;

  // [!! 核心 !!]
  // 填充 K=1 和 K=2 (current 和 peek)
  lexer_scan_token(lexer, &lexer->current);
  lexer_scan_token(lexer, &lexer->peek);
}

/**
 * @brief 消耗当前 Token，使 'peek' 成为 'current'。
 */
void
ir_lexer_next(Lexer *lexer)
{
  // 1. 将 peek 移到 current
  lexer->current = lexer->peek;

  // 2. 如果 current 不是 EOF，扫描下一个 token 到 peek
  if (lexer->current.type != TK_EOF)
  {
    lexer_scan_token(lexer, &lexer->peek);
  }
  // (如果 current 是 EOF, peek 也会是 EOF)
}

/**
 * @brief 获取*当前* Token (K=1)
 */
const Token *
ir_lexer_current_token(const Lexer *lexer)
{
  return &lexer->current;
}

/**
 * @brief 预读*下一个* Token (K=2)
 */
const Token *
ir_lexer_peek_token(const Lexer *lexer)
{
  return &lexer->peek;
}

/**
 * @brief [辅助] "吃掉" 当前 Token，如果它匹配预期类型。
 */
bool
ir_lexer_eat(Lexer *lexer, TokenType expected)
{
  if (lexer->current.type != expected)
  {
    // 'eat' 是一个高级辅助函数，它不应该将
    // token 设为 ILLEGAL，它应该只报告 false。
    // Parser 负责报告错误。
    return false;
  }
  // 消耗匹配的 Token
  ir_lexer_next(lexer);
  return true;
}
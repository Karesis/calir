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
#include "utils/id_list.h"

#include <assert.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

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

  int64_t int_part = 0;
  while (isdigit(current_char(l)))
  {
    int_part = int_part * 10 + (advance(l) - '0');
  }

  if (current_char(l) == '.' && isdigit(peek_char(l)))
  {

    advance(l);

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

    out_token->type = TK_INTEGER_LITERAL;
    out_token->as.int_val = is_negative ? -int_part : int_part;
  }

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

  while (current_char(l) != '"' && current_char(l) != '\0')
  {
    advance(l);
  }

  if (current_char(l) == '\0')
  {

    out_token->type = TK_ILLEGAL;
    return;
  }

  size_t len = l->ptr - start;
  advance(l);

  out_token->type = TK_STRING_LITERAL;

  out_token->as.ident_val = ir_context_intern_str_slice(l->context, start, len);
}

/**
 * @brief  扫描下一个 Token 并填充 out_token。
 */
static void
lexer_scan_token(Lexer *l, Token *out_token)
{

  skip_whitespace(l);

  out_token->line = l->line;
  out_token->column = (l->ptr - l->line_start) + 1;

  char c = advance(l);

  switch (c)
  {

  case '\0':
    out_token->type = TK_EOF;
    break;

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

  case '.':
    if (current_char(l) == '.' && peek_char(l) == '.')
    {
      advance(l);
      advance(l);
      out_token->type = TK_ELLIPSIS;
    }
    else
    {

      out_token->type = TK_ILLEGAL;
    }
    break;

  case '@':
    parse_global_or_local(l, TK_GLOBAL_IDENT, out_token);
    break;
  case '%':
    parse_global_or_local(l, TK_LOCAL_IDENT, out_token);
    break;
  case '$':
    parse_global_or_local(l, TK_LABEL_IDENT, out_token);
    break;

  case '"':
    parse_string(l, out_token);
    break;

  default:

    if (is_ident_start(c))
    {
      l->ptr--;
      parse_ident(l, out_token);
    }

    else if (isdigit(c) || (c == '-' && isdigit(peek_char(l))))
    {

      l->ptr--;
      parse_number(l, out_token);
    }

    else
    {
      out_token->type = TK_ILLEGAL;
    }
    break;
  }
}

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

  lexer_scan_token(lexer, &lexer->current);
  lexer_scan_token(lexer, &lexer->peek);
}

/**
 * @brief 消耗当前 Token，使 'peek' 成为 'current'。
 */
void
ir_lexer_next(Lexer *lexer)
{

  lexer->current = lexer->peek;

  if (lexer->current.type != TK_EOF)
  {
    lexer_scan_token(lexer, &lexer->peek);
  }
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

    return false;
  }

  ir_lexer_next(lexer);
  return true;
}
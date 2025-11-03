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
 * @brief 检查一个标识符是否为关键字
 *
 * (这是一个简单的实现；更快的实现会使用 gperf)
 */
static TokenType
lookup_keyword(const char *ident)
{
  // 这是一个简单的优化，用于减少 strcmp 的调用次数
  // (我们也可以用一个真正的 hashmap，但这需要更多依赖)
  switch (ident[0])
  {
  case 'a':
    if (strcmp(ident, "add") == 0)
      return TK_KW_ADD;
    if (strcmp(ident, "alloca") == 0)
      return TK_KW_ALLOCA;
    if (strcmp(ident, "and") == 0)
      return TK_KW_AND;
    if (strcmp(ident, "ashr") == 0)
      return TK_KW_ASHR;
    break;
  case 'b':
    if (strcmp(ident, "br") == 0)
      return TK_KW_BR;
    if (strcmp(ident, "bitcast") == 0)
      return TK_KW_BITCAST;
    break;
  case 'c':
    if (strcmp(ident, "call") == 0)
      return TK_KW_CALL;
    // 'cond_br' 只是 'br'，由 parser 区分
    break;
  case 'd':
    if (strcmp(ident, "define") == 0)
      return TK_KW_DEFINE;
    if (strcmp(ident, "default") == 0)
      return TK_KW_DEFAULT;
    break;
  case 'e':
    if (strcmp(ident, "eq") == 0)
      return TK_KW_EQ;
    break;
  case 'f':
    if (strcmp(ident, "fadd") == 0)
      return TK_KW_FADD;
    if (strcmp(ident, "fsub") == 0)
      return TK_KW_FSUB;
    if (strcmp(ident, "fmul") == 0)
      return TK_KW_FMUL;
    if (strcmp(ident, "fdiv") == 0)
      return TK_KW_FDIV;
    if (strcmp(ident, "fcmp") == 0)
      return TK_KW_FCMP;
    if (strcmp(ident, "false") == 0)
      return TK_KW_FALSE;
    if (strcmp(ident, "fptrunc") == 0)
      return TK_KW_FPTRUNC;
    if (strcmp(ident, "fpext") == 0)
      return TK_KW_FPEXT;
    if (strcmp(ident, "fptoui") == 0)
      return TK_KW_FPTOUI;
    if (strcmp(ident, "fptosi") == 0)
      return TK_KW_FPTOSI;
    break;
  case 'g':
    if (strcmp(ident, "global") == 0)
      return TK_KW_GLOBAL;
    if (strcmp(ident, "gep") == 0)
      return TK_KW_GEP;
    break;
  case 'i':
    if (strcmp(ident, "icmp") == 0)
      return TK_KW_ICMP;
    if (strcmp(ident, "inbounds") == 0)
      return TK_KW_INBOUNDS;
    if (strcmp(ident, "inttoptr") == 0)
      return TK_KW_INTTOPTR;
    break;
  case 'l':
    if (strcmp(ident, "load") == 0)
      return TK_KW_LOAD;
    if (strcmp(ident, "lshr") == 0)
      return TK_KW_LSHR;
    break;
  case 'm':
    if (strcmp(ident, "module") == 0)
      return TK_KW_MODULE;
    if (strcmp(ident, "mul") == 0)
      return TK_KW_MUL;
    break;
  case 'n':
    if (strcmp(ident, "ne") == 0)
      return TK_KW_NE;
    break;
  case 'o':
    if (strcmp(ident, "or") == 0)
      return TK_KW_OR;
    if (strcmp(ident, "oeq") == 0)
      return TK_KW_OEQ;
    if (strcmp(ident, "ogt") == 0)
      return TK_KW_OGT;
    if (strcmp(ident, "oge") == 0)
      return TK_KW_OGE;
    if (strcmp(ident, "olt") == 0)
      return TK_KW_OLT;
    if (strcmp(ident, "ole") == 0)
      return TK_KW_OLE;
    if (strcmp(ident, "one") == 0)
      return TK_KW_ONE;
    if (strcmp(ident, "ord") == 0)
      return TK_KW_ORD;
    break;
  case 'p':
    if (strcmp(ident, "phi") == 0)
      return TK_KW_PHI;
    if (strcmp(ident, "ptrtoint") == 0)
      return TK_KW_PTRTOINT;
    break;
  case 'r':
    if (strcmp(ident, "ret") == 0)
      return TK_KW_RET;
    break;
  case 's':
    if (strcmp(ident, "sub") == 0)
      return TK_KW_SUB;
    if (strcmp(ident, "sdiv") == 0)
      return TK_KW_SDIV;
    if (strcmp(ident, "srem") == 0)
      return TK_KW_SREM;
    if (strcmp(ident, "shl") == 0)
      return TK_KW_SHL;
    if (strcmp(ident, "store") == 0)
      return TK_KW_STORE;
    if (strcmp(ident, "switch") == 0)
      return TK_KW_SWITCH;
    if (strcmp(ident, "sext") == 0)
      return TK_KW_SEXT;
    if (strcmp(ident, "sitofp") == 0)
      return TK_KW_SITOFP;
    if (strcmp(ident, "sgt") == 0)
      return TK_KW_SGT;
    if (strcmp(ident, "sge") == 0)
      return TK_KW_SGE;
    if (strcmp(ident, "slt") == 0)
      return TK_KW_SLT;
    if (strcmp(ident, "sle") == 0)
      return TK_KW_SLE;
    break;
  case 't':
    if (strcmp(ident, "type") == 0)
      return TK_KW_TYPE;
    if (strcmp(ident, "trunc") == 0)
      return TK_KW_TRUNC;
    if (strcmp(ident, "to") == 0)
      return TK_KW_TO;
    if (strcmp(ident, "true") == 0)
      return TK_KW_TRUE;
    break;
  case 'u':
    if (strcmp(ident, "udiv") == 0)
      return TK_KW_UDIV;
    if (strcmp(ident, "urem") == 0)
      return TK_KW_UREM;
    if (strcmp(ident, "uitofp") == 0)
      return TK_KW_UITOFP;
    if (strcmp(ident, "ugt") == 0)
      return TK_KW_UGT;
    if (strcmp(ident, "uge") == 0)
      return TK_KW_UGE;
    if (strcmp(ident, "ult") == 0)
      return TK_KW_ULT;
    if (strcmp(ident, "ule") == 0)
      return TK_KW_ULE;
    if (strcmp(ident, "ueq") == 0)
      return TK_KW_UEQ;
    if (strcmp(ident, "une") == 0)
      return TK_KW_UNE;
    if (strcmp(ident, "uno") == 0)
      return TK_KW_UNO;
    break;
  case 'x':
    if (strcmp(ident, "xor") == 0)
      return TK_KW_XOR;
    break;
  case 'z':
    if (strcmp(ident, "zext") == 0)
      return TK_KW_ZEXT;
    break;
  }

  // 如果没有匹配，它就是
  return TK_IDENT;
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

  // [!!] 修复:
  // 1. 立即将 (start, len) 切片“驻留” (intern) 到上下文中。
  //    这会返回一个唯一的、以 '\0' 结尾的、可比较的 const char*。
  const char *interned_ident = ir_context_intern_str_slice(l->context, start, len);

  // 2. 使用这个唯一的指针进行关键字查找。
  //    这比在临时缓冲区上操作更干净、更安全。
  out_token->type = lookup_keyword(interned_ident);

  // 3. 只有当它 *不是* 关键字时，我们才需要存储这个值。
  if (out_token->type == TK_IDENT)
  {
    out_token->as.ident_val = interned_ident;
  }
  else
  {
    // 它是关键字 (e.g., TK_KW_ADD), 我们不需要存储值。
    // (我们刚才 interned 的字符串在 context 中是“孤儿”，
    //  但这没关系，interner 会在下次遇到 "add" 时重用它)
    out_token->as.ident_val = NULL;
  }
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
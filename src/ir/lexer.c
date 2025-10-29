#include "ir/lexer.h"
#include "ir/context.h"    // 需要 ir_context_intern_str_slice
#include "utils/id_list.h" // 包含 container_of

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
  // 标识符（如 'add', 'i32', 'my_label'）必须以字母或下划线开头
  return isalpha(c) || c == '_';
}

static bool
is_ident_continue(char c)
{
  // 后续字符可以是字母、数字、下划线或点 (e.g., %x.ptr)
  return isalnum(c) || c == '_' || c == '.';
}

// -----------------------------------------------------------------
// 辅助解析器 (Helper Parsers)
// -----------------------------------------------------------------

// 获取当前字符 (不消耗)
static char
current_char(Lexer *l)
{
  return *l->ptr;
}

// 消耗当前字符并返回它
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

// 获取下一个字符 (不消耗)
static char
peek_char(Lexer *l)
{
  if (*l->ptr == '\0')
  {
    return '\0';
  }
  return *(l->ptr + 1);
}

// 跳过注释 (从 ';' 到行尾)
static void
skip_comment(Lexer *l)
{
  while (current_char(l) != '\n' && current_char(l) != '\0')
  {
    advance(l);
  }
}

// 跳过所有空白字符和注释
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
      break;
    case ';':
      skip_comment(l);
      break;
    default:
      return; // 遇到非空白字符，停止
    }
  }
}

// 解析一个 TK_IDENT (e.g., 'define', 'i32', 'add')
static void
parse_ident(Lexer *l)
{
  const char *start = l->ptr;
  // (我们已经知道第一个字符是 ident_start)
  advance(l);

  while (is_ident_continue(current_char(l)))
  {
    advance(l);
  }
  size_t len = l->ptr - start;

  l->current_token.type = TK_IDENT;
  // [!!] 字符串驻留 (String Interning)
  l->current_token.as.ident_val = ir_context_intern_str_slice(l->context, start, len);
}

// 解析一个 TK_GLOBAL_IDENT 或 TK_LOCAL_IDENT
// (e.g., '@main', '%entry', '%0')
static void
parse_global_or_local(Lexer *l, TokenType type)
{
  // (我们已经知道第一个字符是 '@' 或 '%')
  advance(l); // 跳过 '@' 或 '%'

  const char *start = l->ptr;

  // [!!] 允许数字作为第一个字符 (e.g., %0)
  if (!is_ident_continue(current_char(l)))
  {
    // 错误: 单独的 '@' 或 '%'
    l->current_token.type = TK_ILLEGAL;
    return;
  }

  while (is_ident_continue(current_char(l)))
  {
    advance(l);
  }
  size_t len = l->ptr - start;

  l->current_token.type = type;
  l->current_token.as.ident_val = ir_context_intern_str_slice(l->context, start, len);
}

// 解析一个 TK_INTEGER_LITERAL (e.g., '123' or '-42')
static void
parse_number(Lexer *l)
{
  const char *start = l->ptr;
  bool is_negative = false;

  if (current_char(l) == '-')
  {
    is_negative = true;
    advance(l);
  }

  // (我们已经知道至少有一位数字)
  int64_t val = 0;
  while (isdigit(current_char(l)))
  {
    val = val * 10 + (current_char(l) - '0');
    advance(l);
  }

  l->current_token.type = TK_INTEGER_LITERAL;
  l->current_token.as.int_val = is_negative ? -val : val;
}

// -----------------------------------------------------------------
// 公共 API (Public API)
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

  // "启动" Lexer，加载第一个 Token
  ir_lexer_next(lexer);
}

/**
 * @brief "吃掉" 当前 Token，并让 Lexer 解析下一个 Token。
 */
void
ir_lexer_next(Lexer *lexer)
{
  // 1. 跳过空白和注释
  skip_whitespace(lexer);

  // 2. 存储 Token 的起始行号
  lexer->current_token.line = lexer->line;

  // 3. 消耗一个字符并进行分派
  char c = advance(lexer);

  switch (c)
  {
  // --- 文件结束 ---
  case '\0':
    lexer->current_token.type = TK_EOF;
    break;

  // --- 标点符号 ---
  case '=':
    lexer->current_token.type = TK_EQ;
    break;
  case ',':
    lexer->current_token.type = TK_COMMA;
    break;
  case ':':
    lexer->current_token.type = TK_COLON;
    break;
  case '{':
    lexer->current_token.type = TK_LBRACE;
    break;
  case '}':
    lexer->current_token.type = TK_RBRACE;
    break;
  case '[':
    lexer->current_token.type = TK_LBRACKET;
    break;
  case ']':
    lexer->current_token.type = TK_RBRACKET;
    break;
  case '(':
    lexer->current_token.type = TK_LPAREN;
    break;
  case ')':
    lexer->current_token.type = TK_RPAREN;
    break;

  // --- 标识符 ---
  case '@':
    parse_global_or_local(lexer, TK_GLOBAL_IDENT);
    break;
  case '%':
    parse_global_or_local(lexer, TK_LOCAL_IDENT);
    break;

  // --- 默认情况 (标识符, 数字, 或非法字符) ---
  default:
    // 1. 普通标识符 (e.g., 'define', 'add')
    if (is_ident_start(c))
    {
      // 我们多 'advance' 了一个字符，回退一步
      lexer->ptr--;
      parse_ident(lexer);
    }
    // 2. 数字 (e.g., '123', '-42')
    else if (isdigit(c) || (c == '-' && isdigit(peek_char(lexer))))
    {
      // 回退一步
      lexer->ptr--;
      parse_number(lexer);
    }
    // 3. 非法字符
    else
    {
      lexer->current_token.type = TK_ILLEGAL;
    }
    break;
  }
}

/**
 * @brief [辅助] "吃掉" 当前 Token，如果它匹配预期类型。
 */
bool
ir_lexer_eat(Lexer *lexer, TokenType expected)
{
  if (lexer->current_token.type != expected)
  {
    // [!!] Parser 应该在这里报错
    // 为了安全，我们将 token 设为 ILLEGAL
    lexer->current_token.type = TK_ILLEGAL;
    return false;
  }
  // 消耗匹配的 Token
  ir_lexer_next(lexer);
  return true;
}
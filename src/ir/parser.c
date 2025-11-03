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

/* src/ir/parser.c */

#include "ir/parser.h"
#include "ir/basicblock.h"
#include "ir/builder.h"
#include "ir/constant.h"
#include "ir/context.h"
#include "ir/function.h"
#include "ir/global.h"
#include "ir/instruction.h"
#include "ir/lexer.h"
#include "ir/module.h"
#include "ir/type.h"
#include "ir/value.h"
#include "ir/verifier.h"
#include "utils/bump.h"
#include "utils/hashmap.h"
#include "utils/id_list.h"
#include "utils/temp_vec.h"

#include <assert.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *token_type_to_string(TokenType type);
static void parser_error_at(Parser *p, const Token *tok, const char *format, ...);
static void parser_error(Parser *p, const char *message);
static void print_parse_error(Parser *p, const char *source_buffer);
static const Token *current_token(Parser *p);
static void advance(Parser *p);
static bool match(Parser *p, TokenType type);
static bool expect(Parser *p, TokenType type);
static bool expect_ident(Parser *p, const char *ident_str);
static IRValueNode *parser_find_value(Parser *p, Token *tok);
static void parser_record_value(Parser *p, Token *tok, IRValueNode *val);
/*
 * =================================================================
 * --- 调试辅助 (Debug Helpers) ---
 * =================================================================
 */

static const char *
token_type_to_string(TokenType type)
{
  switch (type)
  {
  case TK_ILLEGAL:
    return "Illegal";
  case TK_EOF:
    return "EOF";
  case TK_IDENT:
    return "Identifier";
  case TK_GLOBAL_IDENT:
    return "GlobalIdentifier (@...)";
  case TK_LOCAL_IDENT:
    return "LocalIdentifier (%...)";
  case TK_LABEL_IDENT:
    return "LabelIdentifier ($...)";
  case TK_INTEGER_LITERAL:
    return "Integer";
  case TK_FLOAT_LITERAL:
    return "Float";
  case TK_STRING_LITERAL:
    return "String";
  case TK_EQ:
    return "'='";
  case TK_COMMA:
    return "','";
  case TK_COLON:
    return "':'";
  case TK_LBRACE:
    return "'{'";
  case TK_RBRACE:
    return "'}'";
  case TK_LBRACKET:
    return "'['";
  case TK_RBRACKET:
    return "']'";
  case TK_LPAREN:
    return "'('";
  case TK_RPAREN:
    return "')'";
  case TK_LT:
    return "'<'";
  case TK_GT:
    return "'>'";
  case TK_ELLIPSIS:
    return "'...'";
  case TK_SEMICOLON:
    return "';'";
  case TK_KW_MODULE:
    return "keyword 'module'";
  case TK_KW_DEFINE:
    return "keyword 'define'";
  case TK_KW_DECLARE:
    return "keyword 'declare'";
  case TK_KW_GLOBAL:
    return "keyword 'global'";
  case TK_KW_TYPE:
    return "keyword 'type'";
  case TK_KW_RET:
    return "keyword 'ret'";
  case TK_KW_BR:
    return "keyword 'br'";
  case TK_KW_COND_BR:
    return "keyword 'br' (cond)";
  case TK_KW_SWITCH:
    return "keyword 'switch'";
  case TK_KW_DEFAULT:
    return "keyword 'default'";
  case TK_KW_ADD:
    return "keyword 'add'";
  case TK_KW_SUB:
    return "keyword 'sub'";
  case TK_KW_MUL:
    return "keyword 'mul'";
  case TK_KW_UDIV:
    return "keyword 'udiv'";
  case TK_KW_SDIV:
    return "keyword 'sdiv'";
  case TK_KW_UREM:
    return "keyword 'urem'";
  case TK_KW_SREM:
    return "keyword 'srem'";
  case TK_KW_FADD:
    return "keyword 'fadd'";
  case TK_KW_FSUB:
    return "keyword 'fsub'";
  case TK_KW_FMUL:
    return "keyword 'fmul'";
  case TK_KW_FDIV:
    return "keyword 'fdiv'";
  case TK_KW_SHL:
    return "keyword 'shl'";
  case TK_KW_LSHR:
    return "keyword 'lshr'";
  case TK_KW_ASHR:
    return "keyword 'ashr'";
  case TK_KW_AND:
    return "keyword 'and'";
  case TK_KW_OR:
    return "keyword 'or'";
  case TK_KW_XOR:
    return "keyword 'xor'";
  case TK_KW_ALLOCA:
    return "keyword 'alloca'";
  case TK_KW_LOAD:
    return "keyword 'load'";
  case TK_KW_STORE:
    return "keyword 'store'";
  case TK_KW_GEP:
    return "keyword 'gep'";
  case TK_KW_INBOUNDS:
    return "keyword 'inbounds'";
  case TK_KW_ICMP:
    return "keyword 'icmp'";
  case TK_KW_FCMP:
    return "keyword 'fcmp'";
  case TK_KW_TRUNC:
    return "keyword 'trunc'";
  case TK_KW_ZEXT:
    return "keyword 'zext'";
  case TK_KW_SEXT:
    return "keyword 'sext'";
  case TK_KW_FPTRUNC:
    return "keyword 'fptrunc'";
  case TK_KW_FPEXT:
    return "keyword 'fpext'";
  case TK_KW_FPTOUI:
    return "keyword 'fptoui'";
  case TK_KW_FPTOSI:
    return "keyword 'fptosi'";
  case TK_KW_UITOFP:
    return "keyword 'uitofp'";
  case TK_KW_SITOFP:
    return "keyword 'sitofp'";
  case TK_KW_PTRTOINT:
    return "keyword 'ptrtoint'";
  case TK_KW_INTTOPTR:
    return "keyword 'inttoptr'";
  case TK_KW_BITCAST:
    return "keyword 'bitcast'";
  case TK_KW_TO:
    return "keyword 'to'";
  case TK_KW_PHI:
    return "keyword 'phi'";
  case TK_KW_CALL:
    return "keyword 'call'";
  case TK_KW_UNDEF:
    return "keyword 'undef'";
  case TK_KW_NULL:
    return "keyword 'null'";
  case TK_KW_ZEROINITIALIZER:
    return "keyword 'zeroinitializer'";
  case TK_KW_EQ:
    return "predicate 'eq'";
  case TK_KW_NE:
    return "predicate 'ne'";
  case TK_KW_UGT:
    return "predicate 'ugt'";
  case TK_KW_UGE:
    return "predicate 'uge'";
  case TK_KW_ULT:
    return "predicate 'ult'";
  case TK_KW_ULE:
    return "predicate 'ule'";
  case TK_KW_SGT:
    return "predicate 'sgt'";
  case TK_KW_SGE:
    return "predicate 'sge'";
  case TK_KW_SLT:
    return "predicate 'slt'";
  case TK_KW_SLE:
    return "predicate 'sle'";
  case TK_KW_OEQ:
    return "predicate 'oeq'";
  case TK_KW_OGT:
    return "predicate 'ogt'";
  case TK_KW_OGE:
    return "predicate 'oge'";
  case TK_KW_OLT:
    return "predicate 'olt'";
  case TK_KW_OLE:
    return "predicate 'ole'";
  case TK_KW_ONE:
    return "predicate 'one'";
  case TK_KW_UEQ:
    return "predicate 'ueq'";
  case TK_KW_UNE:
    return "predicate 'une'";
  case TK_KW_ORD:
    return "predicate 'ord'";
  case TK_KW_UNO:
    return "predicate 'uno'";
  case TK_KW_TRUE:
    return "keyword 'true'";
  case TK_KW_FALSE:
    return "keyword 'false'";
  default:
    return "Unknown Token";
  }
}

/*
 * =================================================================
 * --- 解析器核心辅助函数 (Parser Core Helpers) ---
 * =================================================================
 */

/**
 * @brief 在指定的 Token 位置报告一个格式化的解析错误
 *
 * @param p Parser
 * @param tok 导致错误的 Token
 * @param format printf 格式的错误信息
 * @param ...
 */
static void
parser_error_at(Parser *p, const Token *tok, const char *format, ...)
{

  if (p->has_error)
  {
    return;
  }
  p->has_error = true;
  p->error.line = tok->line;
  p->error.column = tok->column;

  va_list args;
  va_start(args, format);
  vsnprintf(p->error.message, sizeof(p->error.message), format, args);
  va_end(args);
}

/**
 * @brief 报告一个解析错误 (在*当前* Token 位置)
 *
 * (旧的 parser_error 现在是一个辅助函数)
 *
 * @param p Parser
 * @param message 错误信息
 */
static void
parser_error(Parser *p, const char *message)
{
  parser_error_at(p, current_token(p), "%s", message);
}

/**
 * @brief 在解析失败后，打印详细的诊断信息
 *
 * @param p 解析失败的 Parser
 * @param source_buffer 完整的源文件 C 字符串
 */
static void
print_parse_error(Parser *p, const char *source_buffer)
{
  if (!p->has_error)
    return;

  const char *line_start = source_buffer;
  for (size_t i = 1; i < p->error.line; ++i)
  {
    line_start = strchr(line_start, '\n');
    if (!line_start)
    {

      fprintf(stderr, "Internal Error: Failed to find error line.\n");
      return;
    }
    line_start++;
  }

  const char *line_end = strchr(line_start, '\n');
  if (!line_end)
  {
    line_end = line_start + strlen(line_start);
  }

  int line_len = (int)(line_end - line_start);

  fprintf(stderr, "\n--- Parse Error ---\n");
  fprintf(stderr, "Error: %zu:%zu: %s\n", p->error.line, p->error.column, p->error.message);

  fprintf(stderr, "  |\n");
  fprintf(stderr, "%zu | %.*s\n", p->error.line, line_len, line_start);
  fprintf(stderr, "  | ");

  for (size_t i = 0; i < p->error.column - 1; ++i)
  {

    if (line_start[i] == '\t')
    {
      fprintf(stderr, "\t");
    }
    else
    {
      fprintf(stderr, " ");
    }
  }
  fprintf(stderr, "^\n\n");
}

/**
 * @brief 获取当前 Token (不消耗)
 */
static const Token *
current_token(Parser *p)
{
  return ir_lexer_current_token(p->lexer);
}

/**
 * @brief 消耗当前 Token，前进到下一个
 */
static void
advance(Parser *p)
{
  ir_lexer_next(p->lexer);
}

/**
 * @brief 检查当前 Token 是否匹配预期类型。
 *
 * 如果匹配，消耗它并返回 true。
 * 如果不匹配，不消耗并返回 false。
 * (用于可选的 Token，如逗号)
 *
 * @param p Parser
 * @param type 预期的 Token 类型
 * @return bool 是否匹配
 */
static bool
match(Parser *p, TokenType type)
{
  if (current_token(p)->type == type)
  {
    advance(p);
    return true;
  }
  return false;
}

/**
 * @brief 期望当前 Token 匹配预期类型。
 *
 * 如果匹配，消耗它并返回 true。
 * 如果不匹配，报告错误并返回 false。
 * (用于必需的 Token，如 '{' 或 '=')
 *
 * @param p Parser
 * @param type 预期的 Token 类型
 * @return bool 是否成功
 */
static bool
expect(Parser *p, TokenType type)
{
  const Token *tok = current_token(p);
  if (match(p, type))
  {
    return true;
  }

  parser_error_at(p, tok, "Expected %s, but got %s", token_type_to_string(type), token_type_to_string(tok->type));

  return false;
}

/**
 * @brief 期望当前 Token 是一个特定的标识符 (如 "define", "add")。
 *
 * @param p Parser
 * @param ident_str 期望的 C 字符串
 * @return bool 是否成功
 */
static bool
expect_ident(Parser *p, const char *ident_str)
{
  const Token *tok = current_token(p);

  if (tok->type == TK_IDENT && strcmp(tok->as.ident_val, ident_str) == 0)
  {
    advance(p);
    return true;
  }

  const char *got = (tok->type == TK_IDENT) ? tok->as.ident_val : token_type_to_string(tok->type);
  parser_error_at(p, tok, "Expected identifier '%s', but got '%s'", ident_str, got);
  return false;
}

/**
 * @brief 在符号表 (全局或局部) 中查找一个值。
 *
 * @param p Parser
 * @param tok (TK_GLOBAL_IDENT 或 TK_LOCAL_IDENT)
 * @return IRValueNode* 找到的值，如果未定义则返回 NULL (并报告错误)。
 */
static IRValueNode *
parser_find_value(Parser *p, Token *tok)
{
  assert(tok->type == TK_GLOBAL_IDENT || tok->type == TK_LOCAL_IDENT);

  const char *name = tok->as.ident_val;
  void *val_ptr = NULL;

  if (tok->type == TK_GLOBAL_IDENT)
  {
    val_ptr = ptr_hashmap_get(p->global_value_map, (void *)name);
  }
  else
  {
    if (p->local_value_map)
    {
      val_ptr = ptr_hashmap_get(p->local_value_map, (void *)name);
    }
  }

  if (val_ptr == NULL)
  {
    parser_error_at(p, tok, "Use of undefined value '%c%s'", (tok->type == TK_GLOBAL_IDENT) ? '@' : '%', name);
  }
  return (IRValueNode *)val_ptr;
}

/**
 * @brief 在符号表中注册一个新定义的值。
 *
 * @param p Parser
 * @param tok (TK_GLOBAL_IDENT 或 TK_LOCAL_IDENT)
 * @param val 要注册的 IRValueNode
 */
static void
parser_record_value(Parser *p, Token *tok, IRValueNode *val)
{
  assert(tok->type == TK_GLOBAL_IDENT || tok->type == TK_LOCAL_IDENT);
  const char *name = tok->as.ident_val;
  PtrHashMap *map = (tok->type == TK_GLOBAL_IDENT) ? p->global_value_map : p->local_value_map;

  if (map == NULL)
  {
    parser_error_at(p, tok, "Attempted to define a local value '%%%s' outside a function", name);
    return;
  }

  if (ptr_hashmap_contains(map, (void *)name))
  {
    parser_error_at(p, tok, "Redefinition of value '%c%s'", (tok->type == TK_GLOBAL_IDENT) ? '@' : '%', name);
    return;
  }

  ir_value_set_name(val, name);

  if (!ptr_hashmap_put(map, (void *)name, (void *)val))
  {
    parser_error_at(p, tok, "Failed to record value '%c%s' (HashMap OOM)", (tok->type == TK_GLOBAL_IDENT) ? '@' : '%',
                    name);
  }
}

/**
 * @brief 检查是否为整数类型
 */
static inline bool
ir_type_is_integer(IRType *ty)
{
  return (ty->kind >= IR_TYPE_I1 && ty->kind <= IR_TYPE_I64);
}

/**
 * @brief 检查是否为浮点类型
 */
static inline bool
ir_type_is_floating(IRType *ty)
{
  return (ty->kind == IR_TYPE_F32 || ty->kind == IR_TYPE_F64);
}

/*
 * =================================================================
 * --- 解析器生命周期 (Parser Lifecycle) ---
 * =================================================================
 */

/**
 * @brief 初始化 Parser 结构体
 *
 * @param p Parser
 * @param lexer 已经初始化的 Lexer
 * @param ctx IRContext
 * @param mod 正在构建的 Module
 * @param b 共享的 Builder
 * @return bool 是否成功 (OOM?)
 */
static bool
parser_init(Parser *p, Lexer *lexer, IRContext *ctx, IRModule *mod, IRBuilder *b)
{
  p->lexer = lexer;
  p->context = ctx;
  p->module = mod;
  p->builder = b;
  p->current_function = NULL;
  p->has_error = false;

  bump_init(&p->temp_arena);

  p->global_value_map = ptr_hashmap_create(&ctx->ir_arena, 64);
  if (!p->global_value_map)
  {
    return false;
  }

  p->local_value_map = NULL;

  return true;
}

/**
 * @brief 销毁 Parser (释放其拥有的资源)
 */
static void
parser_destroy(Parser *p)
{

  bump_destroy(&p->temp_arena);

  p->lexer = NULL;
  p->context = NULL;
  p->module = NULL;
  p->builder = NULL;
  p->global_value_map = NULL;
  p->local_value_map = NULL;
}

/*
 * =================================================================
 * --- 递归下降存根 (Recursive Descent Stubs) ---
 * =================================================================
 */

static void parse_module_body(Parser *p);
static void parse_top_level_element(Parser *p);
static void parse_function_definition(Parser *p);
static void parse_function_declaration(Parser *p);
static void parse_global_variable(Parser *p);
static void parse_basic_block(Parser *p);
static void parse_type_definition(Parser *p);
static IRType *parse_function_type(Parser *p, IRType *ret_type);
static IRValueNode *parse_instruction(Parser *p, bool *out_is_terminator);
static IRValueNode *parse_operation(Parser *p, Token *result_token, IRType *result_type, bool *out_is_terminator);
static IRType *parse_type(Parser *p);
static IRValueNode *parse_operand(Parser *p);

/**
 * @brief 主循环：解析模块的顶层元素。
 *
 * module_body ::= top_level_element* EOF
 */
static void
parse_module_body(Parser *p)
{
  while (current_token(p)->type != TK_EOF)
  {
    parse_top_level_element(p);
    if (p->has_error)
    {

      break;
    }
  }
}

/*
 * -----------------------------------------------------------------
 * --- 辅助数据结构 (Helper Structs) ---
 * -----------------------------------------------------------------
 */

/** @brief 临时存储已解析的函数参数 (用于 parse_function_definition) */
typedef struct
{
  IRType *type;
  Token name_tok;
} ParsedArgument;

/*
 * -----------------------------------------------------------------
 * --- 顶层元素解析 (Top-Level Parsing) ---
 * -----------------------------------------------------------------
 */

/**
 * @brief 调度器：解析一个顶层元素。
 *
 * top_level_element ::=
 * 'define' ... (function_definition)
 * | 'declare' ... (function_declaration)
 * | '@' ... (global_variable)
 * | 'type' ... (type_definition)
 */
static void
parse_top_level_element(Parser *p)
{
  const Token *tok = current_token(p);

  switch (tok->type)
  {
  case TK_KW_DEFINE:

    parse_function_definition(p);
    break;

  case TK_KW_DECLARE:

    parse_function_declaration(p);
    break;

  case TK_GLOBAL_IDENT:

    parse_global_variable(p);
    break;

  case TK_LOCAL_IDENT:

    parse_type_definition(p);
    break;

  default:
    parser_error_at(p, tok, "Expected 'define', 'declare', '@name' or '%%name' at top level, but got %s",
                    token_type_to_string(tok->type));
    advance(p);
    break;
  }
}

/**
 * @brief [已重构] 解析一个函数定义
 *
 * 语法: `define <ret_type> @<name> ( <arg_list> ) { ... }`
 * arg_list: `%arg1: type1, %arg2: type2, ...`
 */
static void
parse_function_definition(Parser *p)
{
  advance(p);

  IRType *ret_type = parse_type(p);
  if (!ret_type)
    return;

  Token name_tok = *current_token(p);
  if (!expect(p, TK_GLOBAL_IDENT))
    return;

  IRFunction *func = ir_function_create(p->module, name_tok.as.ident_val, ret_type);
  if (!func)
  {
    parser_error_at(p, &name_tok, "OOM creating function '@%s'", name_tok.as.ident_val);
    return;
  }
  parser_record_value(p, &name_tok, &func->entry_address);

  p->current_function = func;
  bump_reset(&p->temp_arena);
  p->local_value_map = ptr_hashmap_create(&p->temp_arena, 64);
  if (!p->local_value_map)
  {
    parser_error_at(p, &name_tok, "OOM creating local value map for function '@%s'", name_tok.as.ident_val);
    return;
  }

  if (!expect(p, TK_LPAREN))
    return;

  bool is_variadic = false;
  if (current_token(p)->type != TK_RPAREN)
  {
    while (true)
    {
      if (match(p, TK_ELLIPSIS))
      {
        is_variadic = true;
        if (!expect(p, TK_RPAREN))
          return;
        break;
      }

      Token arg_name_tok = *current_token(p);
      if (!expect(p, TK_LOCAL_IDENT))
        return;
      if (!expect(p, TK_COLON))
        return;
      IRType *arg_type = parse_type(p);
      if (!arg_type)
        return;

      IRArgument *arg = ir_argument_create(func, arg_type, arg_name_tok.as.ident_val);
      if (!arg)
      {
        parser_error_at(p, &arg_name_tok, "OOM creating argument '%%%s'", arg_name_tok.as.ident_val);
        return;
      }
      parser_record_value(p, &arg_name_tok, &arg->value);

      if (match(p, TK_RPAREN))
        break;
      if (!expect(p, TK_COMMA))
        return;
    }
  }
  else
  {
    advance(p);
  }

  ir_function_finalize_signature(func, is_variadic);

  if (!expect(p, TK_LBRACE))
    return;
  while (current_token(p)->type != TK_RBRACE && current_token(p)->type != TK_EOF)
  {
    if (p->has_error)
      break;

    if (current_token(p)->type == TK_LABEL_IDENT && ir_lexer_peek_token(p->lexer)->type == TK_COLON)
    {
      parse_basic_block(p);
    }
    else
    {
      parser_error(p, "Expected basic block label (e.g., $entry:)");
      break;
    }
  }
  if (!expect(p, TK_RBRACE))
    return;

  p->current_function = NULL;
  p->local_value_map = NULL;
  bump_reset(&p->temp_arena);
}

/**
 * @brief [!!] 新增: 解析一个函数类型
 *
 * 语法: `<ret_type> ( <param_types> )`
 * @param p Parser (已消耗 <ret_type>)
 * @param ret_type 已解析的返回类型
 */
static IRType *
parse_function_type(Parser *p, IRType *ret_type)
{
  if (!expect(p, TK_LPAREN))
    return NULL;

  bump_reset(&p->temp_arena);
  TempVec params;
  temp_vec_init(&params, &p->temp_arena);
  bool is_variadic = false;

  if (current_token(p)->type != TK_RPAREN)
  {
    while (true)
    {

      if (match(p, TK_ELLIPSIS))
      {
        is_variadic = true;
        if (!expect(p, TK_RPAREN))
          return NULL;
        break;
      }

      IRType *param_type = parse_type(p);
      if (!param_type)
        return NULL;

      if (!temp_vec_push(&params, (void *)param_type))
      {
        parser_error(p, "OOM parsing function parameters");
        return NULL;
      }

      if (match(p, TK_RPAREN))
        break;
      if (!expect(p, TK_COMMA))
        return NULL;
    }
  }
  else
  {
    advance(p);
  }

  IRType **permanent_params = BUMP_ALLOC_SLICE_COPY(&p->context->permanent_arena, IRType *,
                                                    (IRType **)temp_vec_data(&params), temp_vec_len(&params));
  if (temp_vec_len(&params) > 0 && !permanent_params)
  {
    parser_error(p, "OOM copying function parameters");
    return NULL;
  }

  return ir_type_get_function(p->context, ret_type, permanent_params, temp_vec_len(&params), is_variadic);
}

/**
 * @brief 解析一个函数声明 (无函数体)
 *
 * 语法: `declare <ret_type> @<name> ( <type_list> )`
 *
 * @param p Parser (当前 token 是 'declare')
 */
static void
parse_function_declaration(Parser *p)
{
  advance(p);
  IRType *ret_type = parse_type(p);
  if (!ret_type)
    return;

  Token name_tok = *current_token(p);
  if (!expect(p, TK_GLOBAL_IDENT))
    return;

  IRFunction *func = ir_function_create(p->module, name_tok.as.ident_val, ret_type);
  if (!func)
  {
    parser_error_at(p, &name_tok, "OOM creating function declaration '@%s'", name_tok.as.ident_val);
    return;
  }

  parser_record_value(p, &name_tok, &func->entry_address);

  if (!expect(p, TK_LPAREN))
    return;

  bool is_variadic = false;
  if (current_token(p)->type != TK_RPAREN)
  {
    while (true)
    {
      if (match(p, TK_ELLIPSIS))
      {
        is_variadic = true;
        if (!expect(p, TK_RPAREN))
          return;
        break;
      }

      Token arg_name_tok = *current_token(p);
      IRType *arg_type = NULL;
      const char *arg_name = NULL;

      if (arg_name_tok.type == TK_LOCAL_IDENT)
      {

        advance(p);
        if (!expect(p, TK_COLON))
          return;
        arg_type = parse_type(p);
        if (!arg_type)
          return;
        arg_name = arg_name_tok.as.ident_val;
      }
      else
      {

        arg_type = parse_type(p);
        if (!arg_type)
          return;
      }

      ir_argument_create(func, arg_type, arg_name);

      if (match(p, TK_RPAREN))
        break;
      if (!expect(p, TK_COMMA))
        return;
    }
  }
  else
  {
    advance(p);
  }

  ir_function_finalize_signature(func, is_variadic);
  bump_reset(&p->temp_arena);
}
/*
 * -----------------------------------------------------------------
 * --- 函数体解析 (Function Body Parsing) ---
 * -----------------------------------------------------------------
 */

/**
 * @brief [已重构] 解析一个命名类型 (结构体) 定义
 *
 * 语法: `%<name> = 'type' { <type_list> }`
 *
 * @param p Parser (当前 token 是 TK_LOCAL_IDENT)
 */
static void
parse_type_definition(Parser *p)
{

  Token name_tok = *current_token(p);
  if (!expect(p, TK_LOCAL_IDENT))
    return;
  const char *name = name_tok.as.ident_val;

  if (!expect(p, TK_EQ))
    return;

  if (!expect(p, TK_KW_TYPE))
    return;

  if (!expect(p, TK_LBRACE))
    return;

  bump_reset(&p->temp_arena);
  TempVec members;
  temp_vec_init(&members, &p->temp_arena);

  if (current_token(p)->type == TK_RBRACE)
  {
    advance(p);
  }
  else
  {
    while (true)
    {

      IRType *member_type = parse_type(p);
      if (!member_type)
        return;
      if (!temp_vec_push(&members, (void *)member_type))
      {
        parser_error(p, "OOM parsing struct members");
        return;
      }

      if (match(p, TK_RBRACE))
        break;
      if (!expect(p, TK_COMMA))
        return;
    }
  }

  IRType **permanent_members = BUMP_ALLOC_SLICE_COPY(&p->context->permanent_arena, IRType *,
                                                     (IRType **)temp_vec_data(&members), temp_vec_len(&members));
  if (temp_vec_len(&members) > 0 && !permanent_members)
  {
    parser_error(p, "OOM in permanent_arena copying struct members");
    return;
  }

  IRType *named_struct = ir_type_get_named_struct(p->context, name, permanent_members, temp_vec_len(&members));

  if (named_struct == NULL)
  {

    parser_error_at(p, &name_tok, "Failed to create or register named struct '%%%s' (possibly redefined?)", name);
    return;
  }
}

/**
 * @brief [已重构] 解析一个全局变量定义 (设计 B)
 *
 * 语法: `@<name>: <ptr_type> = 'global' <constant_operand>`
 * e.g., `@gvar: <i32> = global 10: i32`
 * e.g., `@garr: <[2xi32]> = global undef: <[2xi32]>`
 *
 * @param p Parser (当前 token 是 TK_GLOBAL_IDENT)
 */
static void
parse_global_variable(Parser *p)
{

  Token name_tok = *current_token(p);
  if (!expect(p, TK_GLOBAL_IDENT))
    return;

  if (!expect(p, TK_COLON))
    return;
  IRType *ptr_type = parse_type(p);
  if (!ptr_type || ptr_type->kind != IR_TYPE_PTR)
  {
    parser_error_at(p, &name_tok, "Global variable '@%s' must have a pointer type annotation (e.g., '@g: <i32> = ...')",
                    name_tok.as.ident_val);
    return;
  }

  IRType *allocated_type = ptr_type->as.pointee_type;

  if (!expect(p, TK_EQ))
    return;

  if (!expect(p, TK_KW_GLOBAL))
    return;

  IRValueNode *initializer = NULL;
  const Token *val_tok = current_token(p);

  if (val_tok->type == TK_KW_ZEROINITIALIZER)
  {
    advance(p);
  }
  else
  {

    initializer = parse_operand(p);
    if (!initializer)
      return;

    if (initializer->kind != IR_KIND_CONSTANT)
    {
      parser_error_at(p, &name_tok, "Initializer for global '@%s' must be a constant operand", name_tok.as.ident_val);
      return;
    }
    if (initializer->type != allocated_type)
    {
      parser_error_at(p, &name_tok, "Initializer's type for global '@%s' does not match allocated type",
                      name_tok.as.ident_val);
      return;
    }
  }

  IRGlobalVariable *gvar = ir_global_variable_create(p->module, name_tok.as.ident_val, allocated_type, initializer);
  if (gvar == NULL)
  {
    parser_error_at(p, &name_tok, "Failed to create global variable object '@%s' (OOM?)", name_tok.as.ident_val);
    return;
  }

  if (gvar->value.type != ptr_type)
  {
    parser_error_at(p, &name_tok, "Internal: GVar creation type mismatch for '@%s'", name_tok.as.ident_val);
    return;
  }

  parser_record_value(p, &name_tok, (IRValueNode *)gvar);
}

/**
 * @brief 解析一个基本块
 *
 * 语法: `$label: instruction*`
 */
static void
parse_basic_block(Parser *p)
{
  if (!p->current_function)
  {
    parser_error(p, "Basic block definition found outside of a function");
    return;
  }

  Token name_tok = *current_token(p);
  if (!expect(p, TK_LABEL_IDENT))
    return;
  if (!expect(p, TK_COLON))
    return;

  const char *name = name_tok.as.ident_val;

  IRBasicBlock *bb = NULL;
  IRValueNode *existing_val = (IRValueNode *)ptr_hashmap_get(p->local_value_map, (void *)name);

  if (existing_val)
  {
    if (existing_val->kind != IR_KIND_BASIC_BLOCK)
    {
      parser_error_at(p, &name_tok, "Label '$%s' conflicts with an existing value", name_tok.as.ident_val);
      return;
    }
    bb = container_of(existing_val, IRBasicBlock, label_address);
    if (bb->list_node.next != &bb->list_node)
    {
      parser_error_at(p, &name_tok, "Redefinition of basic block label '$%s'", name);
      return;
    }
  }
  else
  {
    bb = ir_basic_block_create(p->current_function, name);
    if (!bb)
    {
      parser_error_at(p, &name_tok, "OOM creating basic block '$%s'", name_tok.as.ident_val);
      return;
    }

    ptr_hashmap_put(p->local_value_map, (void *)name, (void *)&bb->label_address);
  }

  ir_function_append_basic_block(p->current_function, bb);

  ir_builder_set_insertion_point(p->builder, bb);

  while (true)
  {
    if (p->has_error)
      return;

    const Token *tok = current_token(p);
    // 检查是否到达函数末尾或文件末尾
    if (tok->type == TK_RBRACE || tok->type == TK_EOF)
      return;

    if (tok->type == TK_LABEL_IDENT && ir_lexer_peek_token(p->lexer)->type == TK_COLON)
    {
      return;
    }

    bool is_terminator = false;
    parse_instruction(p, &is_terminator);

    if (p->has_error)
      return;

    if (is_terminator)
    {
      const Token *next_tok = current_token(p);
      if (next_tok->type != TK_RBRACE && next_tok->type != TK_LABEL_IDENT && next_tok->type != TK_EOF)
      {
        parser_error(p, "Instructions are not allowed after a terminator");
      }
      return;
    }
  }
}

/**
 * @brief [重构] 解析一条指令 (调度器)
 *
 * 语法:
 * [%res: type =] <opcode> ...
 * | <opcode> ...
 */
static IRValueNode *
parse_instruction(Parser *p, bool *out_is_terminator)
{
  Token result_tok;
  IRType *result_type = NULL;
  bool has_result = false;

  Token tok = *current_token(p);
  Token peek_tok = *ir_lexer_peek_token(p->lexer);

  if (tok.type == TK_LOCAL_IDENT && peek_tok.type == TK_COLON)
  {
    result_tok = tok;
    advance(p);
    advance(p);

    result_type = parse_type(p);
    if (!result_type)
      return NULL;

    if (!expect(p, TK_EQ))
      return NULL;

    has_result = true;
  }

  else if (tok.type == TK_LOCAL_IDENT && peek_tok.type == TK_EQ)
  {
    parser_error(p, "Missing type annotation on result (expected '%name: type =')");
    return NULL;
  }

  IRValueNode *instr_val = parse_operation(p, has_result ? &result_tok : NULL, result_type, out_is_terminator);

  if (has_result && instr_val)
  {

    if (instr_val->type != result_type)
    {

      parser_error_at(p, &result_tok, "Instruction result type does not match type annotation for '%%%s'",
                      result_tok.as.ident_val);
      return NULL;
    }

    assert(instr_val->kind == IR_KIND_INSTRUCTION);
    IRInstruction *inst = container_of(instr_val, IRInstruction, result);
    if (inst->opcode != IR_OP_PHI)
    {
      parser_record_value(p, &result_tok, instr_val);
    }
  }

  else if (!has_result && instr_val && instr_val->type->kind != IR_TYPE_VOID)
  {
    parser_error(p, "Instruction produces a value but has no assignment (expected '%res: type = ...')");
    return NULL;
  }

  else if (has_result && instr_val && instr_val->type->kind == IR_TYPE_VOID)
  {
    parser_error_at(p, &result_tok, "Cannot assign result of 'void' instruction to variable '%%%s'",
                    result_tok.as.ident_val);
    return NULL;
  }

  return instr_val;
}
/**
 * @brief [辅助] 解析 ICMP 谓词
 * e.g., "eq", "ne", "slt", "sgt", ...
 */
static IRICmpPredicate
parse_icmp_predicate(Parser *p)
{
  const Token tok = *current_token(p);
  advance(p);

  switch (tok.type)
  {
  case TK_KW_EQ:
    return IR_ICMP_EQ;
  case TK_KW_NE:
    return IR_ICMP_NE;
  case TK_KW_UGT:
    return IR_ICMP_UGT;
  case TK_KW_UGE:
    return IR_ICMP_UGE;
  case TK_KW_ULT:
    return IR_ICMP_ULT;
  case TK_KW_ULE:
    return IR_ICMP_ULE;
  case TK_KW_SGT:
    return IR_ICMP_SGT;
  case TK_KW_SGE:
    return IR_ICMP_SGE;
  case TK_KW_SLT:
    return IR_ICMP_SLT;
  case TK_KW_SLE:
    return IR_ICMP_SLE;
  default:
    parser_error_at(p, &tok, "Unknown ICMP predicate '%s'", token_type_to_string(tok.type));
    return IR_ICMP_EQ; /// default
  }
}

/**
 * @brief [!!] (新增) 解析 FCMP 谓词
 * e.g., "oeq", "ugt", "uno", ...
 */
static IRFCmpPredicate
parse_fcmp_predicate(Parser *p)
{
  const Token tok = *current_token(p);
  advance(p); // 消耗谓词

  switch (tok.type)
  {
  /// --- FCMP 独有 ---
  case TK_KW_OEQ:
    return IR_FCMP_OEQ;
  case TK_KW_OGT:
    return IR_FCMP_OGT;
  case TK_KW_OGE:
    return IR_FCMP_OGE;
  case TK_KW_OLT:
    return IR_FCMP_OLT;
  case TK_KW_OLE:
    return IR_FCMP_OLE;
  case TK_KW_ONE:
    return IR_FCMP_ONE;
  case TK_KW_UEQ:
    return IR_FCMP_UEQ;
  case TK_KW_UNE:
    return IR_FCMP_UNE;
  case TK_KW_ORD:
    return IR_FCMP_ORD;
  case TK_KW_UNO:
    return IR_FCMP_UNO;
  case TK_KW_TRUE:
    return IR_FCMP_TRUE;
  case TK_KW_FALSE:
    return IR_FCMP_FALSE;

  /// --- 共享谓词 ---
  case TK_KW_UGT:
    return IR_FCMP_UGT;
  case TK_KW_UGE:
    return IR_FCMP_UGE;
  case TK_KW_ULT:
    return IR_FCMP_ULT;
  case TK_KW_ULE:
    return IR_FCMP_ULE;

  default:
    parser_error_at(p, &tok, "Unknown or invalid FCMP predicate '%s'", token_type_to_string(tok.type));
    return IR_FCMP_OEQ; /// default
  }
}

static IRValueNode *parse_instr_ret(Parser *p);
static IRValueNode *parse_instr_br(Parser *p);
static IRValueNode *parse_instr_icmp(Parser *p, const char *name_hint, IRType *result_type);
static IRValueNode *parse_instr_alloca(Parser *p, const char *name_hint, IRType *result_type);
static IRValueNode *parse_instr_load(Parser *p, const char *name_hint, IRType *result_type);
static IRValueNode *parse_instr_store(Parser *p);
static IRValueNode *parse_instr_gep(Parser *p, const char *name_hint, IRType *result_type);
static IRValueNode *parse_instr_phi(Parser *p, Token *result_token, IRType *result_type);
static IRValueNode *parse_instr_call(Parser *p, const char *name_hint, IRType *result_type);
static IRValueNode *parse_instr_binary_op(Parser *p, IROpcode op, const char *name_hint, IRType *result_type);
static IRValueNode *parse_instr_cast_op(Parser *p, IROpcode op, const char *name_hint, IRType *result_type);
static IRValueNode *parse_instr_fcmp(Parser *p, const char *name_hint, IRType *result_type);
static IRValueNode *parse_instr_switch(Parser *p);

/**
 * @brief 解析一个操作 (指令的核心)
 *
 * 语法: <opcode> <args>
 * e.g., "add i32 %a, %b"
 *
 * @param p Parser (当前 token 是 opcode)
 * @param result_token 如果有 %result =，则为该 Token；否则为 NULL
 * @param out_is_terminator [输出] 如果解析的指令是终结者，则设为 true
 * @return IRValueNode* 指向新创建的指令
 */
static IRValueNode *
parse_operation(Parser *p, Token *result_token, IRType *result_type, bool *out_is_terminator)
{
  *out_is_terminator = false;

  Token opcode_tok = *current_token(p);
  advance(p); // 消耗指令关键字

  const char *name_hint = result_token ? result_token->as.ident_val : NULL;

  switch (opcode_tok.type)
  {
  /// --- 终结者 ---
  case TK_KW_RET:
    *out_is_terminator = true;
    return parse_instr_ret(p); // (复用你现有的)
  case TK_KW_BR:
    *out_is_terminator = true;
    return parse_instr_br(p); // (复用你现有的)
  case TK_KW_SWITCH:
    *out_is_terminator = true;
    return parse_instr_switch(p);

  /// --- 内存 ---
  case TK_KW_ALLOCA:
    return parse_instr_alloca(p, name_hint, result_type); // (复用你现有的)
  case TK_KW_LOAD:
    return parse_instr_load(p, name_hint, result_type); // (复用你现有的)
  case TK_KW_STORE:
    return parse_instr_store(p); // (复用你现有的)
  case TK_KW_GEP:
    return parse_instr_gep(p, name_hint, result_type); // (已更新)

  /// --- 比较 ---
  case TK_KW_ICMP:
    return parse_instr_icmp(p, name_hint, result_type); // (已更新)
  case TK_KW_FCMP:                                      // [!!] 新增
    return parse_instr_fcmp(p, name_hint, result_type);

  /// --- 整数/位运算 ---
  case TK_KW_ADD:
    return parse_instr_binary_op(p, IR_OP_ADD, name_hint, result_type);
  case TK_KW_SUB:
    return parse_instr_binary_op(p, IR_OP_SUB, name_hint, result_type);
  case TK_KW_MUL:
    return parse_instr_binary_op(p, IR_OP_MUL, name_hint, result_type);
  case TK_KW_UDIV:
    return parse_instr_binary_op(p, IR_OP_UDIV, name_hint, result_type);
  case TK_KW_SDIV:
    return parse_instr_binary_op(p, IR_OP_SDIV, name_hint, result_type);
  case TK_KW_UREM:
    return parse_instr_binary_op(p, IR_OP_UREM, name_hint, result_type);
  case TK_KW_SREM:
    return parse_instr_binary_op(p, IR_OP_SREM, name_hint, result_type);
  case TK_KW_SHL:
    return parse_instr_binary_op(p, IR_OP_SHL, name_hint, result_type);
  case TK_KW_LSHR:
    return parse_instr_binary_op(p, IR_OP_LSHR, name_hint, result_type);
  case TK_KW_ASHR:
    return parse_instr_binary_op(p, IR_OP_ASHR, name_hint, result_type);
  case TK_KW_AND:
    return parse_instr_binary_op(p, IR_OP_AND, name_hint, result_type);
  case TK_KW_OR:
    return parse_instr_binary_op(p, IR_OP_OR, name_hint, result_type);
  case TK_KW_XOR:
    return parse_instr_binary_op(p, IR_OP_XOR, name_hint, result_type);

  /// --- 浮点运算  ---
  case TK_KW_FADD:
    return parse_instr_binary_op(p, IR_OP_FADD, name_hint, result_type);
  case TK_KW_FSUB:
    return parse_instr_binary_op(p, IR_OP_FSUB, name_hint, result_type);
  case TK_KW_FMUL:
    return parse_instr_binary_op(p, IR_OP_FMUL, name_hint, result_type);
  case TK_KW_FDIV:
    return parse_instr_binary_op(p, IR_OP_FDIV, name_hint, result_type);

  /// --- 类型转换  ---
  case TK_KW_TRUNC:
    return parse_instr_cast_op(p, IR_OP_TRUNC, name_hint, result_type);
  case TK_KW_ZEXT:
    return parse_instr_cast_op(p, IR_OP_ZEXT, name_hint, result_type);
  case TK_KW_SEXT:
    return parse_instr_cast_op(p, IR_OP_SEXT, name_hint, result_type);
  case TK_KW_FPTRUNC:
    return parse_instr_cast_op(p, IR_OP_FPTRUNC, name_hint, result_type);
  case TK_KW_FPEXT:
    return parse_instr_cast_op(p, IR_OP_FPEXT, name_hint, result_type);
  case TK_KW_FPTOUI:
    return parse_instr_cast_op(p, IR_OP_FPTOUI, name_hint, result_type);
  case TK_KW_FPTOSI:
    return parse_instr_cast_op(p, IR_OP_FPTOSI, name_hint, result_type);
  case TK_KW_UITOFP:
    return parse_instr_cast_op(p, IR_OP_UITOFP, name_hint, result_type);
  case TK_KW_SITOFP:
    return parse_instr_cast_op(p, IR_OP_SITOFP, name_hint, result_type);
  case TK_KW_PTRTOINT:
    return parse_instr_cast_op(p, IR_OP_PTRTOINT, name_hint, result_type);
  case TK_KW_INTTOPTR:
    return parse_instr_cast_op(p, IR_OP_INTTOPTR, name_hint, result_type);
  case TK_KW_BITCAST:
    return parse_instr_cast_op(p, IR_OP_BITCAST, name_hint, result_type);

  /// --- 其他 ---
  case TK_KW_PHI:
    return parse_instr_phi(p, result_token, result_type); // (复用你现有的)
  case TK_KW_CALL:
    return parse_instr_call(p, name_hint, result_type); // (复用你现有的)

  default:
    parser_error_at(p, &opcode_tok, "Unknown instruction opcode '%s'", token_type_to_string(opcode_tok.type));
    return NULL;
  }
}

/**
 * @brief [新] 解析 'ret'
 * 语法: ret <operand> | ret void
 */
static IRValueNode *
parse_instr_ret(Parser *p)
{
  if (match(p, TK_KW_VOID))
  {
    if (p->current_function->return_type->kind != IR_TYPE_VOID)
    {
      parser_error(p, "Return type mismatch: expected 'void'");
      return NULL;
    }
    return ir_builder_create_ret(p->builder, NULL);
  }

  // 如果不是 'void', 把它当作一个普通的操作数解析
  IRValueNode *ret_val = parse_operand(p);
  if (!ret_val)
    return NULL;

  if (ret_val->type != p->current_function->return_type)
  {
    parser_error(p, "Return value's type does not match function's return type");
    return NULL;
  }

  return ir_builder_create_ret(p->builder, ret_val);
}

/**
 * @brief [新] 解析 'br'
 * 语法: br $label | br %cond: i1, $label, $label
 */
static IRValueNode *
parse_instr_br(Parser *p)
{
  Token tok = *current_token(p);

  if (tok.type == TK_LABEL_IDENT)
  {
    IRValueNode *dest = parse_operand(p);
    if (!dest)
      return NULL;
    return ir_builder_create_br(p->builder, dest);
  }

  IRValueNode *cond = parse_operand(p);
  if (!cond)
    return NULL;
  if (cond->type->kind != IR_TYPE_I1)
  {
    parser_error(p, "Branch condition must be 'i1' type");
    return NULL;
  }

  if (!expect(p, TK_COMMA))
    return NULL;
  IRValueNode *true_dest = parse_operand(p);
  if (!true_dest || true_dest->kind != IR_KIND_BASIC_BLOCK)
  {
    parser_error(p, "Expected $label for 'true' branch");
    return NULL;
  }

  if (!expect(p, TK_COMMA))
    return NULL;
  IRValueNode *false_dest = parse_operand(p);
  if (!false_dest || false_dest->kind != IR_KIND_BASIC_BLOCK)
  {
    parser_error(p, "Expected $label for 'false' branch");
    return NULL;
  }

  return ir_builder_create_cond_br(p->builder, cond, true_dest, false_dest);
}

/// --- 通用二元运算解析器 ---
static IRValueNode *
parse_instr_binary_op(Parser *p, IROpcode op, const char *name_hint, IRType *result_type)
{
  if (!result_type)
  {
    parser_error(p, "Binary instruction must produce a result");
    return NULL;
  }

  IRValueNode *lhs = parse_operand(p);
  if (!lhs)
    return NULL;
  if (!expect(p, TK_COMMA))
    return NULL;
  IRValueNode *rhs = parse_operand(p);
  if (!rhs)
    return NULL;

  if (lhs->type != result_type || rhs->type != result_type)
  {
    parser_error(p, "Operands types must match result type for binary op");
    return NULL;
  }

  // 使用我们的 builder API
  switch (op)
  {
  // Int
  case IR_OP_ADD:
    return ir_builder_create_add(p->builder, lhs, rhs, name_hint);
  case IR_OP_SUB:
    return ir_builder_create_sub(p->builder, lhs, rhs, name_hint);
  case IR_OP_MUL:
    return ir_builder_create_mul(p->builder, lhs, rhs, name_hint);
  case IR_OP_UDIV:
    return ir_builder_create_udiv(p->builder, lhs, rhs, name_hint);
  case IR_OP_SDIV:
    return ir_builder_create_sdiv(p->builder, lhs, rhs, name_hint);
  case IR_OP_UREM:
    return ir_builder_create_urem(p->builder, lhs, rhs, name_hint);
  case IR_OP_SREM:
    return ir_builder_create_srem(p->builder, lhs, rhs, name_hint);
  // Float
  case IR_OP_FADD:
    return ir_builder_create_fadd(p->builder, lhs, rhs, name_hint);
  case IR_OP_FSUB:
    return ir_builder_create_fsub(p->builder, lhs, rhs, name_hint);
  case IR_OP_FMUL:
    return ir_builder_create_fmul(p->builder, lhs, rhs, name_hint);
  case IR_OP_FDIV:
    return ir_builder_create_fdiv(p->builder, lhs, rhs, name_hint);
  // Bitwise
  case IR_OP_SHL:
    return ir_builder_create_shl(p->builder, lhs, rhs, name_hint);
  case IR_OP_LSHR:
    return ir_builder_create_lshr(p->builder, lhs, rhs, name_hint);
  case IR_OP_ASHR:
    return ir_builder_create_ashr(p->builder, lhs, rhs, name_hint);
  case IR_OP_AND:
    return ir_builder_create_and(p->builder, lhs, rhs, name_hint);
  case IR_OP_OR:
    return ir_builder_create_or(p->builder, lhs, rhs, name_hint);
  case IR_OP_XOR:
    return ir_builder_create_xor(p->builder, lhs, rhs, name_hint);
  default:
    parser_error(p, "Internal: unhandled binary op in parser");
    return NULL;
  }
}

/// --- 通用类型转换解析器 ---
static IRValueNode *
parse_instr_cast_op(Parser *p, IROpcode op, const char *name_hint, IRType *result_type)
{
  if (!result_type)
  {
    parser_error(p, "Cast instruction must produce a result");
    return NULL;
  }

  IRValueNode *val = parse_operand(p);
  if (!val)
    return NULL;

  if (!expect(p, TK_KW_TO))
    return NULL;

  IRType *dest_type = parse_type(p);
  if (!dest_type)
    return NULL;

  if (dest_type != result_type)
  {
    parser_error(p, "Cast 'to <type>' does not match result type annotation");
    return NULL;
  }

  // 使用我们的 builder API
  switch (op)
  {
  case IR_OP_TRUNC:
    return ir_builder_create_trunc(p->builder, val, dest_type, name_hint);
  case IR_OP_ZEXT:
    return ir_builder_create_zext(p->builder, val, dest_type, name_hint);
  case IR_OP_SEXT:
    return ir_builder_create_sext(p->builder, val, dest_type, name_hint);
  case IR_OP_FPTRUNC:
    return ir_builder_create_fptrunc(p->builder, val, dest_type, name_hint);
  case IR_OP_FPEXT:
    return ir_builder_create_fpext(p->builder, val, dest_type, name_hint);
  case IR_OP_FPTOUI:
    return ir_builder_create_fptoui(p->builder, val, dest_type, name_hint);
  case IR_OP_FPTOSI:
    return ir_builder_create_fptosi(p->builder, val, dest_type, name_hint);
  case IR_OP_UITOFP:
    return ir_builder_create_uitofp(p->builder, val, dest_type, name_hint);
  case IR_OP_SITOFP:
    return ir_builder_create_sitofp(p->builder, val, dest_type, name_hint);
  case IR_OP_PTRTOINT:
    return ir_builder_create_ptrtoint(p->builder, val, dest_type, name_hint);
  case IR_OP_INTTOPTR:
    return ir_builder_create_inttoptr(p->builder, val, dest_type, name_hint);
  case IR_OP_BITCAST:
    return ir_builder_create_bitcast(p->builder, val, dest_type, name_hint);
  default:
    parser_error(p, "Internal: unhandled cast op in parser");
    return NULL;
  }
}

static IRValueNode *
parse_instr_icmp(Parser *p, const char *name_hint, IRType *result_type)
{
  if (!result_type || result_type->kind != IR_TYPE_I1)
  {
    parser_error(p, "'icmp' must produce an 'i1' result");
    return NULL;
  }
  // [!!] (已更新)
  IRICmpPredicate pred = parse_icmp_predicate(p);

  IRValueNode *lhs = parse_operand(p);
  if (!lhs)
    return NULL;
  if (!expect(p, TK_COMMA))
    return NULL;
  IRValueNode *rhs = parse_operand(p);
  if (!rhs)
    return NULL;
  if (lhs->type != rhs->type)
  {
    parser_error(p, "Operands types must match for 'icmp'");
    return NULL;
  }
  return ir_builder_create_icmp(p->builder, pred, lhs, rhs, name_hint);
}

// --- [!!] (新增) `parse_instr_fcmp` ---
static IRValueNode *
parse_instr_fcmp(Parser *p, const char *name_hint, IRType *result_type)
{
  if (!result_type || result_type->kind != IR_TYPE_I1)
  {
    parser_error(p, "'fcmp' must produce an 'i1' result");
    return NULL;
  }

  IRFCmpPredicate pred = parse_fcmp_predicate(p); // [!!] (新增)

  IRValueNode *lhs = parse_operand(p);
  if (!lhs)
    return NULL;
  if (!expect(p, TK_COMMA))
    return NULL;
  IRValueNode *rhs = parse_operand(p);
  if (!rhs)
    return NULL;

  if (lhs->type != rhs->type)
  {
    parser_error(p, "Operands types must match for 'fcmp'");
    return NULL;
  }
  if (!ir_type_is_floating(lhs->type))
  {
    parser_error(p, "Operands for 'fcmp' must be floating point types");
    return NULL;
  }

  return ir_builder_create_fcmp(p->builder, pred, lhs, rhs, name_hint);
}

static IRValueNode *
parse_instr_alloca(Parser *p, const char *name_hint, IRType *result_type)
{

  IRType *alloc_type = parse_type(p);
  if (!alloc_type)
    return NULL;

  if (!result_type || result_type->kind != IR_TYPE_PTR || result_type->as.pointee_type != alloc_type)
  {
    parser_error(p, "alloca result must be a pointer to the allocated type");
    return NULL;
  }
  return ir_builder_create_alloca(p->builder, alloc_type, name_hint);
}

static IRValueNode *
parse_instr_load(Parser *p, const char *name_hint, IRType *result_type)
{

  if (!result_type)
  {
    parser_error(p, "load must produce a result");
    return NULL;
  }

  IRValueNode *ptr = parse_operand(p);
  if (!ptr)
    return NULL;

  if (ptr->type->kind != IR_TYPE_PTR || ptr->type->as.pointee_type != result_type)
  {
    parser_error(p, "load result type does not match pointer's pointee type");
    return NULL;
  }
  return ir_builder_create_load(p->builder, ptr, name_hint);
}

static IRValueNode *
parse_instr_store(Parser *p)
{

  IRValueNode *val = parse_operand(p);
  if (!val)
    return NULL;
  if (!expect(p, TK_COMMA))
    return NULL;
  IRValueNode *ptr = parse_operand(p);
  if (!ptr)
    return NULL;

  if (ptr->type->kind != IR_TYPE_PTR || ptr->type->as.pointee_type != val->type)
  {
    parser_error(p, "store value type does not match pointer's pointee type");
    return NULL;
  }
  return ir_builder_create_store(p->builder, val, ptr);
}

/**
 * @brief [新] 解析 'gep' (设计 B)
 * 语法: %res: ptr = gep [inbounds] %base: ptr, %idx1: i32, %idx2: i64, ...
 */
static IRValueNode *
parse_instr_gep(Parser *p, const char *name_hint, IRType *result_type)
{
  if (!result_type || result_type->kind != IR_TYPE_PTR)
  {
    parser_error(p, "gep instruction must produce a pointer result");
    return NULL;
  }

  bool inbounds = match(p, TK_KW_INBOUNDS);

  IRValueNode *base_ptr = parse_operand(p);
  if (!base_ptr || base_ptr->type->kind != IR_TYPE_PTR)
  {
    parser_error(p, "gep base operand must be a pointer (%ptr: <type>)");
    return NULL;
  }

  IRType *source_type = base_ptr->type->as.pointee_type;

  bump_reset(&p->temp_arena);
  TempVec indices;
  temp_vec_init(&indices, &p->temp_arena);

  while (match(p, TK_COMMA))
  {

    IRValueNode *idx_val = parse_operand(p);
    if (!idx_val)
    {
      parser_error(p, "Expected GEP index operand");
      return NULL;
    }

    if (idx_val->type->kind < IR_TYPE_I1 || idx_val->type->kind > IR_TYPE_I64)
    {
      parser_error(p, "GEP indices must be integer types");
      return NULL;
    }
    if (!temp_vec_push(&indices, (void *)idx_val))
    {
      parser_error(p, "OOM for GEP indices");
      return NULL;
    }
  }

  if (temp_vec_len(&indices) == 0)
  {
    parser_error(p, "gep must have at least one index operand");
    return NULL;
  }

  return ir_builder_create_gep(p->builder, source_type, base_ptr, (IRValueNode **)temp_vec_data(&indices),
                               temp_vec_len(&indices), inbounds, name_hint);
}

static IRValueNode *
parse_instr_switch(Parser *p)
{
  /// switch %cond: i32, default $dest [ ... ]

  IRValueNode *cond = parse_operand(p);
  if (!cond)
    return NULL;
  if (!ir_type_is_integer(cond->type))
  {
    parser_error(p, "switch condition must be an integer");
    return NULL;
  }

  if (!expect(p, TK_COMMA))
    return NULL;
  if (!expect(p, TK_KW_DEFAULT))
    return NULL;

  IRValueNode *default_bb = parse_operand(p);
  if (!default_bb || default_bb->kind != IR_KIND_BASIC_BLOCK)
  {
    parser_error(p, "switch default must be a basic block label");
    return NULL;
  }

  IRValueNode *switch_inst = ir_builder_create_switch(p->builder, cond, default_bb);

  if (!expect(p, TK_LBRACKET))
    return NULL;

  while (current_token(p)->type != TK_RBRACKET)
  {
    /// <const>: <type>, $dest

    IRValueNode *case_val = parse_operand(p);
    if (!case_val || case_val->kind != IR_KIND_CONSTANT)
    {
      parser_error(p, "switch case value must be a constant");
      return NULL;
    }
    if (case_val->type != cond->type)
    {
      parser_error(p, "switch case value type must match condition type");
      return NULL;
    }

    if (!expect(p, TK_COMMA))
      return NULL;

    IRValueNode *case_bb = parse_operand(p);
    if (!case_bb || case_bb->kind != IR_KIND_BASIC_BLOCK)
    {
      parser_error(p, "switch case destination must be a basic block label");
      return NULL;
    }

    ir_switch_add_case(switch_inst, case_val, case_bb);
  }

  if (!expect(p, TK_RBRACKET))
    return NULL;

  return switch_inst;
}

/**
 * @brief [新] 解析 'phi' (设计 B)
 * 语法: %res: type = phi [ %val1: type, $bb1 ], [ const: type, $bb2 ], ...
 */
static IRValueNode *
parse_instr_phi(Parser *p, Token *result_token, IRType *result_type)
{
  if (!result_type)
  {
    parser_error(p, "phi instruction must produce a result");
    return NULL;
  }
  const char *name_hint = result_token ? result_token->as.ident_val : NULL;

  IRValueNode *phi_node = ir_builder_create_phi(p->builder, result_type, name_hint);
  if (!phi_node)
    return NULL;

  if (result_token)
  {
    parser_record_value(p, result_token, phi_node);
  }

  if (current_token(p)->type != TK_LBRACKET)
  {
    parser_error(p, "phi instruction must have at least one incoming value");
    return NULL;
  }

  while (true)
  {
    if (!expect(p, TK_LBRACKET))
      goto phi_error;

    IRValueNode *val = parse_operand(p);
    if (!val)
      goto phi_error;
    if (val->type != result_type)
    {
      parser_error(p, "PHI incoming value's type does not match PHI result type");
      goto phi_error;
    }

    if (!expect(p, TK_COMMA))
      goto phi_error;

    IRValueNode *bb_val = parse_operand(p);
    if (!bb_val || bb_val->kind != IR_KIND_BASIC_BLOCK)
    {
      parser_error(p, "Expected incoming basic block label ($name) in PHI node");
      goto phi_error;
    }
    IRBasicBlock *bb = container_of(bb_val, IRBasicBlock, label_address);

    ir_phi_add_incoming(phi_node, val, bb);

    if (!expect(p, TK_RBRACKET))
      goto phi_error;

    if (match(p, TK_COMMA) == false)
      break;
  }
  return phi_node;

phi_error:

  parser_error(p, "Failed to parse PHI node incoming values");
  return NULL;
}

/**
 * @brief 解析 'call'
 *
 * 语法 :
 * %res: type = call <func_ptr_type> %callee( %arg1: type1, ... )
 */
static IRValueNode *
parse_instr_call(Parser *p, const char *name_hint, IRType *result_type)
{
  IRType *func_ptr_type = parse_type(p);
  if (!func_ptr_type || func_ptr_type->kind != IR_TYPE_PTR || func_ptr_type->as.pointee_type->kind != IR_TYPE_FUNCTION)
  {
    parser_error(p, "Expected pointer-to-function type (e.g., '<i32(i32)>') before callee");
    return NULL;
  }
  IRType *func_type = func_ptr_type->as.pointee_type;

  if (func_type->as.function.return_type != result_type)
  {
    parser_error(p, "Call result type annotation does not match function's return type");
    return NULL;
  }

  Token callee_tok = *current_token(p);
  IRValueNode *callee_val = NULL;
  if (callee_tok.type == TK_LOCAL_IDENT || callee_tok.type == TK_GLOBAL_IDENT)
  {
    advance(p);
    callee_val = parser_find_value(p, &callee_tok);
    if (!callee_val)
      return NULL;
    if (callee_val->type != func_ptr_type)
    {
      parser_error_at(p, &callee_tok, "Callee's type does not match explicit function pointer type");
      return NULL;
    }
  }
  else
  {
    parser_error(p, "Expected callee name (%func_ptr or @func) after type");
    return NULL;
  }

  if (!expect(p, TK_LPAREN))
    return NULL;

  bump_reset(&p->temp_arena);
  TempVec arg_values;
  temp_vec_init(&arg_values, &p->temp_arena);

  bool is_variadic = func_type->as.function.is_variadic;
  size_t expected_count = func_type->as.function.param_count;

  if (current_token(p)->type != TK_RPAREN)
  {
    while (true)
    {

      IRValueNode *arg_val = parse_operand(p);
      if (!arg_val)
        return NULL;
      IRType *arg_type = arg_val->type;

      if (!is_variadic && temp_vec_len(&arg_values) >= expected_count)
      {
        parser_error(p, "Too many arguments");
        return NULL;
      }
      if (temp_vec_len(&arg_values) < expected_count)
      {
        if (arg_type != func_type->as.function.param_types[temp_vec_len(&arg_values)])
        {
          parser_error(p, "Argument type mismatch in call");
          return NULL;
        }
      }
      if (!temp_vec_push(&arg_values, (void *)arg_val))
      {
        parser_error(p, "OOM parsing call arguments");
        return NULL;
      }

      if (match(p, TK_RPAREN))
        break;
      if (!expect(p, TK_COMMA))
        return NULL;
    }
  }
  else
  {
    advance(p);
  }

  if (temp_vec_len(&arg_values) < expected_count)
  {
    if (is_variadic)
    {
      parser_error_at(p, &callee_tok, "Too few arguments for variadic call: expected at least %zu, got %zu",
                      expected_count, temp_vec_len(&arg_values));
    }
    else
    {
      parser_error_at(p, &callee_tok, "Too few arguments for call: expected %zu, got %zu", expected_count,
                      temp_vec_len(&arg_values));
    }
    return NULL;
  }

  return ir_builder_create_call(p->builder, callee_val, (IRValueNode **)temp_vec_data(&arg_values),
                                temp_vec_len(&arg_values), name_hint);
}
/*
 * -----------------------------------------------------------------
 * --- 类型解析 (Type Parsing) ---
 * -----------------------------------------------------------------
 */

/**
 * @brief 解析一个数组类型
 *
 * 语法: `[ <count> x <type> ]`
 * @param p Parser (已消耗 '[')
 */
static IRType *
parse_array_type(Parser *p)
{

  const Token *count_tok = current_token(p);
  if (!expect(p, TK_INTEGER_LITERAL))
  {
    return NULL;
  }
  size_t count = (size_t)count_tok->as.int_val;

  if (count_tok->as.int_val < 0)
  {
    parser_error_at(p, count_tok, "Array size cannot be negative (got %" PRId64 ")", count_tok->as.int_val);
    return NULL;
  }

  if (!expect_ident(p, "x"))
  {
    return NULL;
  }

  IRType *element_type = parse_type(p);
  if (!element_type)
  {
    return NULL;
  }

  if (!expect(p, TK_RBRACKET))
  {
    return NULL;
  }

  return ir_type_get_array(p->context, element_type, count);
}

/**
 * @brief 解析一个匿名结构体类型
 *
 * 语法: `{ <type1>, <type2>, ... }`
 * @param p Parser (已消耗 '{')
 */
static IRType *
parse_struct_type(Parser *p)
{

  bump_reset(&p->temp_arena);
  TempVec members;
  temp_vec_init(&members, &p->temp_arena);

  if (current_token(p)->type == TK_RBRACE)
  {
    advance(p);
    return ir_type_get_anonymous_struct(p->context, NULL, 0);
  }

  while (true)
  {

    IRType *member_type = parse_type(p);
    if (!member_type)
    {
      return NULL;
    }
    if (!temp_vec_push(&members, (void *)member_type))
    {
      parser_error(p, "OOM parsing anonymous struct members");
      return NULL;
    }

    if (match(p, TK_RBRACE))
    {
      break;
    }

    if (!expect(p, TK_COMMA))
    {
      return NULL;
    }
  }

  IRType **permanent_members = BUMP_ALLOC_SLICE_COPY(&p->context->permanent_arena, IRType *,
                                                     (IRType **)temp_vec_data(&members), temp_vec_len(&members));
  if (temp_vec_len(&members) > 0 && !permanent_members)
  {
    parser_error(p, "OOM in permanent_arena copying struct members");
    return NULL;
  }

  return ir_type_get_anonymous_struct(p->context, permanent_members, temp_vec_len(&members));
}

/**
 * @brief 解析一个类型签名
 *
 * type ::=
 * 'void' | 'i1' | 'i8' | 'i16' | 'i32' | 'i64' | 'f32' | 'f64'
 * | '<' <type> '>'  // 指针
 * | '[' <count> 'x' <type> ']'
 * | '{' <type>, ... '}'
 * | '%' <name>  // (命名结构体)
 * | <ret_type> '(' ... ')' // 函数类型
 */
static IRType *
parse_type(Parser *p)
{
  const Token *tok = current_token(p);
  IRType *base_type = NULL;

  switch (tok->type)
  {
  case TK_LT: {
    advance(p);
    IRType *pointee_type = parse_type(p);
    if (!pointee_type)
      return NULL;
    if (!expect(p, TK_GT))
      return NULL;
    base_type = ir_type_get_ptr(p->context, pointee_type);
    break;
  }
  case TK_KW_VOID: {
    advance(p); // 消耗 'void'
    base_type = ir_type_get_void(p->context);
    break;
  }
  case TK_IDENT: {
    const char *name = tok->as.ident_val;

    if (strcmp(name, "i1") == 0)
    {
      base_type = ir_type_get_i1(p->context);
    }
    else if (strcmp(name, "i8") == 0)
    {
      base_type = ir_type_get_i8(p->context);
    }
    else if (strcmp(name, "i16") == 0)
    {
      base_type = ir_type_get_i16(p->context);
    }
    else if (strcmp(name, "i32") == 0)
    {
      base_type = ir_type_get_i32(p->context);
    }
    else if (strcmp(name, "i64") == 0)
    {
      base_type = ir_type_get_i64(p->context);
    }
    else if (strcmp(name, "f32") == 0)
    {
      base_type = ir_type_get_f32(p->context);
    }
    else if (strcmp(name, "f64") == 0)
    {
      base_type = ir_type_get_f64(p->context);
    }
    else
    {
      parser_error_at(p, tok, "Unknown type identifier '%s'", name);
      return NULL;
    }
    advance(p);
    break;
  }

  case TK_LBRACKET:
    advance(p);
    base_type = parse_array_type(p);
    break;

  case TK_LBRACE:
    advance(p);
    base_type = parse_struct_type(p);
    break;

  case TK_LOCAL_IDENT: {

    Token name_tok = *current_token(p);
    advance(p);

    const char *name = name_tok.as.ident_val;

    size_t name_len = strlen(name);

    IRType *found_type = (IRType *)str_hashmap_get(p->context->named_struct_cache, name, name_len);

    if (found_type == NULL)
    {

      parser_error_at(p, &name_tok, "Use of undefined named type '%%%s'", name_tok.as.ident_val);
      return NULL;
    }
    base_type = found_type;
    break;
  }

  default:
    parser_error(p, "Expected a type signature");
    return NULL;
  }

  if (current_token(p)->type == TK_LPAREN)
  {
    return parse_function_type(p, base_type);
  }

  return base_type;
}

/*
 * -----------------------------------------------------------------
 * --- 操作数与常量解析 (Operand & Constant Parsing) ---
 * -----------------------------------------------------------------
 */

/**
 * @brief [新机制] 从 Token 和 Type 创建一个常量
 *
 * (取代了旧 parse_operand 中的 switch 逻辑)
 *
 * @param p Parser
 * @param val_tok 值的 Token (e.g., '10', 'true')
 * @param type 该常量应有的类型 (e.g., i32)
 * @return IRValueNode*
 */
static IRValueNode *
parse_constant_from_token(Parser *p, Token *val_tok, IRType *type)
{
  switch (val_tok->type)
  {
  case TK_INTEGER_LITERAL: {
    int64_t val = val_tok->as.int_val;

    switch (type->kind)
    {
    case IR_TYPE_I1:
      return ir_constant_get_i1(p->context, val != 0);
    case IR_TYPE_I8:
      return ir_constant_get_i8(p->context, (int8_t)val);
    case IR_TYPE_I16:
      return ir_constant_get_i16(p->context, (int16_t)val);
    case IR_TYPE_I32:
      return ir_constant_get_i32(p->context, (int32_t)val);
    case IR_TYPE_I64:
      return ir_constant_get_i64(p->context, val);
    default:
      parser_error_at(p, val_tok, "Integer literal '%" PRId64 "' provided for non-integer type", val_tok->as.int_val);
      return NULL;
    }
  }

  case TK_FLOAT_LITERAL: {
    double val = val_tok->as.float_val;
    switch (type->kind)
    {
    case IR_TYPE_F32:
      return ir_constant_get_f32(p->context, (float)val);
    case IR_TYPE_F64:
      return ir_constant_get_f64(p->context, val);
    default:
      parser_error_at(p, val_tok, "Float literal '%f' provided for non-float type", val_tok->as.float_val);
      return NULL;
    }
  }

  case TK_KW_TRUE:
    if (type->kind != IR_TYPE_I1)
      parser_error_at(p, val_tok, "'true' must have type 'i1'");
    return ir_constant_get_i1(p->context, true);

  case TK_KW_FALSE:
    if (type->kind != IR_TYPE_I1)
      parser_error_at(p, val_tok, "'false' must have type 'i1'");
    return ir_constant_get_i1(p->context, false);

  case TK_KW_UNDEF:
    return ir_constant_get_undef(p->context, type);

  case TK_KW_NULL:
    if (type->kind != IR_TYPE_PTR)
      parser_error_at(p, val_tok, "'null' must have 'ptr' type");
    return ir_constant_get_undef(p->context, type); /// null 也是 undef ?

  default:
    parser_error_at(p, val_tok, "Unexpected token '%s' as constant value", token_type_to_string(val_tok->type));
    return NULL;
  }
}

/**
 * @brief [重构] 解析一个 "类型跟随" 的操作数
 *
 * 语法:
 * %val: type
 * | @val: type
 * | $val
 * | 10: i32
 * | true: i1
 * | undef: type
 *
 * @param p Parser
 * @return IRValueNode*
 */
static IRValueNode *
parse_operand(Parser *p)
{
  Token val_tok = *current_token(p);
  advance(p);

  if (val_tok.type == TK_LABEL_IDENT)
  {
    const char *label_name = val_tok.as.ident_val;
    IRValueNode *val = (IRValueNode *)ptr_hashmap_get(p->local_value_map, (void *)label_name);
    if (!val)
    {

      IRBasicBlock *fwd_bb = ir_basic_block_create(p->current_function, label_name);
      val = (IRValueNode *)&fwd_bb->label_address;
      ptr_hashmap_put(p->local_value_map, (void *)label_name, (void *)val);
    }
    if (val->kind != IR_KIND_BASIC_BLOCK)
    {
      parser_error_at(p, &val_tok, "Expected a basic block label ($name), but '$%s' is not a label",
                      val_tok.as.ident_val);
      return NULL;
    }
    return val;
  }

  if (!expect(p, TK_COLON))
    return NULL;

  IRType *type = parse_type(p);
  if (!type)
    return NULL;

  switch (val_tok.type)
  {

  case TK_LOCAL_IDENT:
  case TK_GLOBAL_IDENT: {
    IRValueNode *val = parser_find_value(p, &val_tok);
    if (!val)
      return NULL;
    if (val->type != type)
    {

      parser_error_at(p, &val_tok, "Variable's type annotation does not match its definition type");
      return NULL;
    }
    return val;
  }

  case TK_INTEGER_LITERAL:
  case TK_FLOAT_LITERAL:
  case TK_IDENT: {
    return parse_constant_from_token(p, &val_tok, type);
  }

  default:
    parser_error_at(p, &val_tok, "Unexpected token '%s' as operand value", token_type_to_string(val_tok.type));
    return NULL;
  }
}

/*
 * =================================================================
 * --- 公共 API (Public API) ---
 * =================================================================
 */

/**
 * @brief 解析一个完整的 IR 模块 (主入口点)
 */
IRModule *
ir_parse_module(IRContext *ctx, const char *source_buffer)
{
  assert(ctx && source_buffer);

  Lexer lexer;
  ir_lexer_init(&lexer, source_buffer, ctx);

  IRBuilder *builder = ir_builder_create(ctx);
  if (!builder)
  {
    fprintf(stderr, "Fatal: Failed to create IRBuilder\n");
    return NULL;
  }

  const char *module_name = "parsed_module";
  const Token *first_tok = ir_lexer_current_token(&lexer);

  if (first_tok->type == TK_KW_MODULE)
  {
    ir_lexer_next(&lexer);

    const Token *eq_tok = ir_lexer_current_token(&lexer);
    if (eq_tok->type != TK_EQ)
    {

      fprintf(stderr, "Parse Error (%zu:%zu): Expected '=' after 'module', but got %s\n", eq_tok->line, eq_tok->column,
              token_type_to_string(eq_tok->type));
      ir_builder_destroy(builder);
      return NULL;
    }
    ir_lexer_next(&lexer);

    const Token *name_tok = ir_lexer_current_token(&lexer);
    if (name_tok->type != TK_STRING_LITERAL)
    {

      fprintf(stderr, "Parse Error (%zu:%zu): Expected string literal (e.g., \"foo.c\") after 'module =', but got %s\n",
              name_tok->line, name_tok->column, token_type_to_string(name_tok->type));
      ir_builder_destroy(builder);
      return NULL;
    }

    module_name = name_tok->as.ident_val;
    ir_lexer_next(&lexer);
  }

  IRModule *module = ir_module_create(ctx, module_name);
  if (!module)
  {
    ir_builder_destroy(builder);
    fprintf(stderr, "Fatal: Failed to create IRModule\n");
    return NULL;
  }

  Parser parser;
  if (!parser_init(&parser, &lexer, ctx, module, builder))
  {
    ir_builder_destroy(builder);
    fprintf(stderr, "Fatal: Failed to init Parser (OOM)\n");

    return NULL;
  }

  parse_module_body(&parser);

  bool success = !parser.has_error;

  if (!success)
  {

    print_parse_error(&parser, source_buffer);
  }

  parser_destroy(&parser);
  ir_builder_destroy(builder);

  if (success)
  {

    if (!ir_verify_module(module))
    {

      fprintf(stderr, "Parser Error: Generated IR failed verification.\n");

      return NULL;
    }
    return module;
  }
  else
  {

    return NULL;
  }
}
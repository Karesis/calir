/* src/ir/parser.c */

#include "ir/parser.h"
#include "ir/basicblock.h" // For ir_basic_block_create
#include "ir/builder.h"    // For ir_builder_create/destroy
#include "ir/constant.h"   // For ir_constant_get_...
#include "ir/context.h"
#include "ir/function.h" // For ir_function_create
#include "ir/global.h"   // For ir_global_variable_create
#include "ir/instruction.h"
#include "ir/lexer.h"
#include "ir/module.h" // For ir_module_create
#include "ir/type.h"
#include "ir/value.h" // For ir_value_set_name
#include "ir/verifier.h"
#include "utils/bump.h"    // For bump_init/destroy
#include "utils/hashmap.h" // For ptr_hashmap_...
#include "utils/id_list.h" // for container_of, list_entry

#include <assert.h>
#include <inttypes.h> // For PRId64
#include <stdio.h>    // For fprintf, stderr, snprintf
#include <stdlib.h>
#include <string.h> // For strcmp

/*
 * =================================================================
 * --- 调试辅助 (Debug Helpers) ---
 * =================================================================
 */

// (可选) 将 Token 类型转换为字符串，用于错误报告
static const char *
token_type_to_string(TokenType type)
{
  // (这是一个简短的列表，可以扩展)
  switch (type)
  {
  case TK_EOF:
    return "EOF";
  case TK_ILLEGAL:
    return "Illegal";
  case TK_IDENT:
    return "Identifier";
  case TK_GLOBAL_IDENT:
    return "GlobalIdentifier (@...)";
  case TK_LOCAL_IDENT:
    return "LocalIdentifier (%...)";
  case TK_INTEGER_LITERAL:
    return "Integer";
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
  case TK_LPAREN:
    return "'('";
  case TK_RPAREN:
    return "')'";
  // ... 其他 Token ...
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
 * @brief 报告一个解析错误
 *
 * @param p Parser
 * @param message 错误信息
 */
static void
parser_error(Parser *p, const char *message)
{
  // 仅报告第一个错误，防止错误雪崩
  if (p->has_error)
  {
    return;
  }
  p->has_error = true;
  fprintf(stderr, "Parse Error (Line %zu): %s\n", p->lexer->current.line, message);
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
  if (match(p, type))
  {
    return true;
  }

  // 格式化错误信息
  char error_msg[256];
  snprintf(error_msg, sizeof(error_msg), "Expected %s, but got %s", token_type_to_string(type),
           token_type_to_string(current_token(p)->type));
  parser_error(p, error_msg);
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
  Token *tok = current_token(p);

  // [假设] Lexer 提供的 `as.ident_val` 是一个 null 结尾的 C 字符串
  if (tok->type == TK_IDENT && strcmp(tok->as.ident_val, ident_str) == 0)
  {
    advance(p);
    return true;
  }

  char error_msg[256];
  snprintf(error_msg, sizeof(error_msg), "Expected identifier '%s', but got '%s'", ident_str,
           (tok->type == TK_IDENT) ? tok->as.ident_val : token_type_to_string(tok->type));
  parser_error(p, error_msg);
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
  // `as.ident_val` 是 Lexer intern 后的唯一指针
  const char *name = tok->as.ident_val;
  void *val_ptr = NULL;

  if (tok->type == TK_GLOBAL_IDENT)
  {
    val_ptr = ptr_hashmap_get(p->global_value_map, (void *)name);
  }
  else // TK_LOCAL_IDENT
  {
    if (p->local_value_map)
    {
      val_ptr = ptr_hashmap_get(p->local_value_map, (void *)name);
    }
  }

  if (val_ptr == NULL)
  {
    char error_msg[256];
    snprintf(error_msg, sizeof(error_msg), "Use of undefined value '%c%s'", (tok->type == TK_GLOBAL_IDENT) ? '@' : '%',
             name);
    parser_error(p, error_msg);
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
  const char *name = tok->as.ident_val; // Interned 指针
  PtrHashMap *map = (tok->type == TK_GLOBAL_IDENT) ? p->global_value_map : p->local_value_map;

  if (map == NULL)
  {
    parser_error(p, "Attempted to define a local value outside a function");
    return;
  }

  // 检查重定义
  if (ptr_hashmap_contains(map, (void *)name))
  {
    char error_msg[256];
    snprintf(error_msg, sizeof(error_msg), "Redefinition of value '%c%s'", (tok->type == TK_GLOBAL_IDENT) ? '@' : '%',
             name);
    parser_error(p, error_msg);
    return;
  }

  // 设置 ValueNode 上的名字 (API 会 strdup 它)
  ir_value_set_name(val, name);

  // 存入符号表 (使用 interned 指针作为 Key)
  if (!ptr_hashmap_put(map, (void *)name, (void *)val))
  {
    parser_error(p, "Failed to record value (HashMap OOM)");
  }
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

  // 初始化临时 Arena (用于局部符号表等)
  bump_init(&p->temp_arena);

  // 初始化全局符号表 (使用 Context 的 IR Arena)
  p->global_value_map = ptr_hashmap_create(&ctx->ir_arena, 64);
  if (!p->global_value_map)
  {
    return false; // OOM
  }

  p->local_value_map = NULL; // 只有进入函数时才创建

  return true;
}

/**
 * @brief 销毁 Parser (释放其拥有的资源)
 */
static void
parser_destroy(Parser *p)
{
  // local_value_map 在 temp_arena 中
  bump_destroy(&p->temp_arena);

  // global_value_map 在 ir_arena 中，由 context 统一管理
  // builder 也是由调用者 (ir_parse_module) 管理

  // 清理指针
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

// --- 前向声明 ---
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
static IRValueNode *parse_constant(Parser *p);

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
      // 发生错误，停止解析
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
  Token *tok = current_token(p);

  switch (tok->type)
  {
  case TK_IDENT:
    if (strcmp(tok->as.ident_val, "define") == 0)
    {
      parse_function_definition(p);
    }
    else if (strcmp(tok->as.ident_val, "declare") == 0)
    {
      parse_function_declaration(p);
    }
    // [!!] 'type' 关键字不再是顶层入口点
    else
    {
      parser_error(p, "Expected 'define' or 'declare' at top level");
      advance(p); // 消耗错误 token
    }
    break;

  case TK_GLOBAL_IDENT:
    // @gvar: <type> = ...
    parse_global_variable(p);
    break;

  // [!!] 修复：添加此分支以处理 '%my_struct = type ...'
  case TK_LOCAL_IDENT:
    // %my_struct = type { ... }
    parse_type_definition(p);
    break;

  default:
    parser_error(p, "Unexpected token at top level");
    advance(p); // 消耗错误 token
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
  advance(p); // 消耗 'define'

  // 1. 解析返回类型
  IRType *ret_type = parse_type(p);
  if (!ret_type)
    return;

  // 2. 解析函数名 @name
  Token name_tok = *current_token(p);
  if (!expect(p, TK_GLOBAL_IDENT))
    return;

  // 3. 创建*未定稿*的函数
  IRFunction *func = ir_function_create(p->module, name_tok.as.ident_val, ret_type);
  if (!func)
  {
    parser_error(p, "OOM creating function");
    return;
  }
  parser_record_value(p, &name_tok, &func->entry_address);

  // 4. [进入函数作用域]
  p->current_function = func;
  bump_reset(&p->temp_arena);
  p->local_value_map = ptr_hashmap_create(&p->temp_arena, 64);
  if (!p->local_value_map)
  {
    parser_error(p, "OOM creating local value map");
    return;
  }

  // 5. 解析参数列表 `(%name: type, ...)`
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

      // [OK] 已修复 (设计 B)
      Token arg_name_tok = *current_token(p);
      if (!expect(p, TK_LOCAL_IDENT))
      {
        parser_error(p, "Expected argument name (e.g., %a) in parameter list");
        return;
      }
      if (!expect(p, TK_COLON))
        return;
      IRType *arg_type = parse_type(p);
      if (!arg_type)
        return;

      IRArgument *arg = ir_argument_create(func, arg_type, arg_name_tok.as.ident_val);
      if (!arg)
      {
        parser_error(p, "OOM creating argument");
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
    advance(p); // 消耗 ')'
  }

  ir_function_finalize_signature(func, is_variadic);

  // 6. 解析函数体
  if (!expect(p, TK_LBRACE))
    return;
  while (current_token(p)->type != TK_RBRACE && current_token(p)->type != TK_EOF)
  {
    if (p->has_error)
      break;

    // [!!] 修复 Bug 1 [!!]
    // 必须检查 TK_LABEL_IDENT, 而不是 TK_IDENT
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

  // 7. [退出函数作用域]
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

  // 1. 解析参数类型列表 (存储在 temp_arena)
  bump_reset(&p->temp_arena);
  size_t capacity = 8;
  size_t count = 0;
  IRType **param_types = BUMP_ALLOC_SLICE(&p->temp_arena, IRType *, capacity);
  bool is_variadic = false;

  if (current_token(p)->type != TK_RPAREN)
  {
    while (true)
    {
      // [!!] 检查可变参数 '...'
      if (match(p, TK_ELLIPSIS)) // (你需要将 '...' 添加到 Lexer: TK_ELLIPSIS)
      {
        is_variadic = true;
        if (!expect(p, TK_RPAREN))
          return NULL;
        break;
      }

      // 扩容
      if (count == capacity)
      { /* ... 扩容逻辑 (你的代码是正确的) ... */
      }

      IRType *param_type = parse_type(p);
      if (!param_type)
        return NULL;
      param_types[count++] = param_type;

      if (match(p, TK_RPAREN))
        break;
      if (!expect(p, TK_COMMA))
        return NULL;
    }
  }
  else
  {
    advance(p); // 消耗 ')'
  }

  // 2. 将参数列表复制到 permanent_arena
  IRType **permanent_params = BUMP_ALLOC_SLICE_COPY(&p->context->permanent_arena, IRType *, param_types, count);
  if (count > 0 && !permanent_params)
  {
    parser_error(p, "OOM copying function parameters");
    return NULL;
  }

  // 3. 获取唯一的函数类型
  return ir_type_get_function(p->context, ret_type, permanent_params, count, is_variadic);
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
  advance(p); // 消耗 'declare'
  IRType *ret_type = parse_type(p);
  if (!ret_type)
    return;

  Token name_tok = *current_token(p);
  if (!expect(p, TK_GLOBAL_IDENT))
    return;

  IRFunction *func = ir_function_create(p->module, name_tok.as.ident_val, ret_type);
  if (!func)
  {
    parser_error(p, "OOM creating function");
    return;
  }

  parser_record_value(p, &name_tok, &func->entry_address);

  // [!!] 修复 Bug 2 [!!]
  // 3. 解析参数列表 `(%name: type, ...)`
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

      // [!!] 已修复 (设计 B)
      Token arg_name_tok = *current_token(p);
      IRType *arg_type = NULL;
      const char *arg_name = NULL;

      if (arg_name_tok.type == TK_LOCAL_IDENT)
      {
        // 语法: %name: type
        advance(p); // 消耗 %name
        if (!expect(p, TK_COLON))
          return;
        arg_type = parse_type(p);
        if (!arg_type)
          return;
        arg_name = arg_name_tok.as.ident_val;
      }
      else
      {
        // 语法: type (e.g. for `declare i32 @printf(<i8*>, ...)`
        arg_type = parse_type(p);
        if (!arg_type)
          return;
        // arg_name 保持为 NULL
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
    advance(p); // 消耗 ')'
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
  // 1. 解析 %name
  Token name_tok = *current_token(p);
  if (!expect(p, TK_LOCAL_IDENT))
    return;
  const char *name = name_tok.as.ident_val;

  // 2. [!! 修复 !!] 解析 =
  if (!expect(p, TK_EQ))
    return;

  // 3. [!! 修复 !!] 解析 'type' 关键字
  if (!expect_ident(p, "type"))
    return;

  // 4. 解析结构体字面量 { ... }
  if (!expect(p, TK_LBRACE))
  {
    parser_error(p, "Expected struct body '{...}' after 'type'");
    return;
  }

  // 5. 解析成员列表 (这部分逻辑是正确的)
  bump_reset(&p->temp_arena);
  size_t capacity = 8;
  size_t count = 0;
  IRType **members = BUMP_ALLOC_SLICE(&p->temp_arena, IRType *, capacity);
  if (!members && capacity > 0)
  {
    parser_error(p, "OOM in temp_arena for struct members");
    return;
  }

  if (current_token(p)->type == TK_RBRACE)
  {
    advance(p); // 消耗 '}' (空结构体)
  }
  else
  {
    while (true)
    {
      if (count == capacity)
      {
        // ... (扩容逻辑) ...
      }

      IRType *member_type = parse_type(p);
      if (!member_type)
        return;
      members[count++] = member_type;

      if (match(p, TK_RBRACE))
        break;
      if (!expect(p, TK_COMMA))
        return;
    }
  }

  // 6. 复制到永久 Arena
  IRType **permanent_members = BUMP_ALLOC_SLICE_COPY(&p->context->permanent_arena, IRType *, members, count);
  if (count > 0 && !permanent_members)
  {
    parser_error(p, "OOM in permanent_arena copying struct members");
    return;
  }

  // 7. [OK] 调用 API
  IRType *named_struct = ir_type_get_named_struct(p->context, name, permanent_members, count);
  if (named_struct == NULL)
  {
    parser_error(p, "Failed to create or register named struct");
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
  // 1. 解析 @name
  Token name_tok = *current_token(p);
  if (!expect(p, TK_GLOBAL_IDENT))
    return;

  // 2. [!! 新增 !!] 期待 ': <type>'
  if (!expect(p, TK_COLON))
    return;
  IRType *ptr_type = parse_type(p);
  if (!ptr_type || ptr_type->kind != IR_TYPE_PTR)
  {
    parser_error(p, "Global variable must have a pointer type annotation (e.g., '@g: <i32> = ...')");
    return;
  }
  // 这是全局变量 *内部* 的类型
  IRType *allocated_type = ptr_type->as.pointee_type;

  // 3. 解析 =
  if (!expect(p, TK_EQ))
    return;

  // 4. 解析 'global' 关键字
  if (!expect_ident(p, "global"))
    return;

  // 5. [!! 已重构 !!] 解析*初始值* (使用 "设计 B")
  IRValueNode *initializer = NULL;
  Token *val_tok = current_token(p);

  if (val_tok->type == TK_IDENT && strcmp(val_tok->as.ident_val, "zeroinitializer") == 0)
  {
    advance(p); // 消耗 'zeroinitializer'
    // initializer 保持为 NULL (ir_global_variable_create 会处理)
  }
  else
  {
    // [!!] 使用我们重构后的 parse_operand
    initializer = parse_operand(p);
    if (!initializer)
      return;
    // 验证
    if (initializer->kind != IR_KIND_CONSTANT)
    {
      parser_error(p, "Global initializer must be a constant operand");
      return;
    }
    if (initializer->type != allocated_type)
    {
      parser_error(p, "Global initializer's type does not match allocated type");
      return;
    }
  }

  // 6. 创建 IRGlobalVariable 对象
  IRGlobalVariable *gvar = ir_global_variable_create(p->module, name_tok.as.ident_val, allocated_type, initializer);
  if (gvar == NULL)
  {
    parser_error(p, "Failed to create global variable object (OOM?)");
    return;
  }

  // 7. [!!] 最终验证
  if (gvar->value.type != ptr_type)
  {
    parser_error(p, "Internal: GVar creation type mismatch (Builder API is wrong?)");
    return;
  }

  // 8. 注册
  parser_record_value(p, &name_tok, (IRValueNode *)gvar);
}

/**
 * @brief [已重构] 解析一个基本块
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

  // [!!] 修复 Bug 1 [!!]
  // 1. 解析标签 ($name:)
  Token name_tok = *current_token(p);
  if (!expect(p, TK_LABEL_IDENT))
  {
    parser_error(p, "Expected basic block label (e.g., $entry)");
    return;
  }
  if (!expect(p, TK_COLON))
    return;

  const char *name = name_tok.as.ident_val; // "entry"

  // 2. 创建 BasicBlock
  IRBasicBlock *bb = NULL;
  IRValueNode *existing_val = (IRValueNode *)ptr_hashmap_get(p->local_value_map, (void *)name);

  if (existing_val) // [OK] 前向声明
  {
    if (existing_val->kind != IR_KIND_BASIC_BLOCK)
    {
      parser_error(p, "Label name conflicts with an existing value");
      return;
    }
    bb = container_of(existing_val, IRBasicBlock, label_address);
    if (bb->list_node.next != &bb->list_node)
    {
      char error_msg[256];
      snprintf(error_msg, sizeof(error_msg), "Redefinition of basic block label '$%s'", name);
      parser_error(p, error_msg);
      return;
    }
  }
  else // [OK] 新标签
  {
    bb = ir_basic_block_create(p->current_function, name);
    if (!bb)
    {
      parser_error(p, "OOM creating basic block");
      return;
    }
    // 注册到符号表
    ptr_hashmap_put(p->local_value_map, (void *)name, (void *)&bb->label_address);
  }

  // 3. [OK] 附加到函数
  ir_function_append_basic_block(p->current_function, bb);

  // 4. [OK] 设置 Builder
  ir_builder_set_insertion_point(p->builder, bb);

  // 5. [OK] 循环解析指令
  while (true)
  {
    if (p->has_error)
      return;

    Token *tok = current_token(p);
    if (tok->type == TK_RBRACE)
      return;

    // [!!] 修复 Bug 1 [!!]
    if (tok->type == TK_LABEL_IDENT && ir_lexer_peek_token(p->lexer)->type == TK_COLON)
    {
      return;
    }

    bool is_terminator = false;
    parse_instruction(p, &is_terminator);

    if (is_terminator)
    {
      if (current_token(p)->type != TK_RBRACE && current_token(p)->type != TK_LABEL_IDENT) // [!!] 修复
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

  // 检查是否为带结果的指令 (设计 B)
  // 语法: %name : type =
  if (tok.type == TK_LOCAL_IDENT && peek_tok.type == TK_COLON)
  {
    result_tok = tok;
    advance(p); // 消耗 %name
    advance(p); // 消耗 :

    result_type = parse_type(p);
    if (!result_type)
      return NULL;

    if (!expect(p, TK_EQ))
      return NULL;

    has_result = true;
  }
  // [!!] 捕获旧的/错误的语法
  else if (tok.type == TK_LOCAL_IDENT && peek_tok.type == TK_EQ)
  {
    parser_error(p, "Missing type annotation on result (expected '%name: type =')");
    return NULL;
  }

  // [新] 将 result_type (或 NULL) 传递给 operation 解析器
  IRValueNode *instr_val = parse_operation(p, has_result ? &result_tok : NULL, result_type, out_is_terminator);

  // 如果有结果，注册到局部符号表
  if (has_result && instr_val)
  {
    // [!!] 验证指令返回的类型是否与我们的注解匹配
    if (instr_val->type != result_type)
    {
      parser_error(p, "Instruction result type does not match type annotation");
      // (这里可能需要打印两种类型)
      return NULL;
    }

    // 注册 (PHI 节点由其自己的解析器内部处理)
    assert(instr_val->kind == IR_KIND_INSTRUCTION);
    IRInstruction *inst = container_of(instr_val, IRInstruction, result);
    if (inst->opcode != IR_OP_PHI)
    {
      parser_record_value(p, &result_tok, instr_val);
    }
  }
  // [!!] 检查: 如果指令 *有* 结果 (e.g. add) 但 *没有*
  // has_result 标记 (e.g. 缺少 %res: type =), 报告错误。
  else if (!has_result && instr_val && instr_val->type->kind != IR_TYPE_VOID)
  {
    parser_error(p, "Instruction produces a value but has no assignment (expected '%res: type = ...')");
    return NULL;
  }
  // (反之亦然: %res: type = ret void)
  else if (has_result && instr_val && instr_val->type->kind == IR_TYPE_VOID)
  {
    parser_error(p, "Cannot assign result of 'void' instruction to a variable");
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
  // 1. 复制当前 Token (因为 expect 会消耗它)
  Token tok = *current_token(p);

  // 2. 消耗 TK_IDENT
  if (!expect(p, TK_IDENT))
    return IR_ICMP_EQ; // 默认，但 expect 已经报告了错误

  // 3. 从 *副本* 中获取谓词字符串
  const char *pred = tok.as.ident_val;

  // Signed
  if (strcmp(pred, "eq") == 0)
    return IR_ICMP_EQ;
  if (strcmp(pred, "ne") == 0)
    return IR_ICMP_NE;
  if (strcmp(pred, "slt") == 0)
    return IR_ICMP_SLT;
  if (strcmp(pred, "sle") == 0)
    return IR_ICMP_SLE;
  if (strcmp(pred, "sgt") == 0)
    return IR_ICMP_SGT;
  if (strcmp(pred, "sge") == 0)
    return IR_ICMP_SGE;

  // Unsigned
  if (strcmp(pred, "ugt") == 0)
    return IR_ICMP_UGT;
  if (strcmp(pred, "uge") == 0)
    return IR_ICMP_UGE;
  if (strcmp(pred, "ult") == 0)
    return IR_ICMP_ULT;
  if (strcmp(pred, "ule") == 0)
    return IR_ICMP_ULE;

  // 错误
  parser_error(p, "Unknown ICMP predicate");
  return IR_ICMP_EQ; // 默认
}

static IRValueNode *parse_instr_ret(Parser *p);
static IRValueNode *parse_instr_br(Parser *p);
static IRValueNode *parse_instr_add(Parser *p, const char *name_hint, IRType *result_type);
static IRValueNode *parse_instr_sub(Parser *p, const char *name_hint, IRType *result_type);
static IRValueNode *parse_instr_icmp(Parser *p, const char *name_hint, IRType *result_type);
static IRValueNode *parse_instr_alloca(Parser *p, const char *name_hint, IRType *result_type);
static IRValueNode *parse_instr_load(Parser *p, const char *name_hint, IRType *result_type);
static IRValueNode *parse_instr_store(Parser *p);
static IRValueNode *parse_instr_gep(Parser *p, const char *name_hint, IRType *result_type);
static IRValueNode *parse_instr_phi(Parser *p, Token *result_token, IRType *result_type);
static IRValueNode *parse_instr_call(Parser *p, const char *name_hint, IRType *result_type);

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
/**
 * @brief [重构] 解析一个操作 (纯分派器)
 *
 * (旧的巨大函数已被拆分)
 */
static IRValueNode *
parse_operation(Parser *p, Token *result_token, IRType *result_type, bool *out_is_terminator)
{
  *out_is_terminator = false;

  Token opcode_tok = *current_token(p);
  if (!expect(p, TK_IDENT))
  {
    parser_error(p, "Expected instruction opcode");
    return NULL;
  }
  const char *opcode = opcode_tok.as.ident_val;
  const char *name_hint = result_token ? result_token->as.ident_val : NULL;

  // --- 分派 ---

  if (strcmp(opcode, "ret") == 0)
  {
    *out_is_terminator = true;
    return parse_instr_ret(p);
  }
  if (strcmp(opcode, "br") == 0)
  {
    *out_is_terminator = true;
    return parse_instr_br(p);
  }
  if (strcmp(opcode, "add") == 0)
  {
    return parse_instr_add(p, name_hint, result_type);
  }
  if (strcmp(opcode, "sub") == 0)
  {
    return parse_instr_sub(p, name_hint, result_type);
  }
  if (strcmp(opcode, "icmp") == 0)
  {
    return parse_instr_icmp(p, name_hint, result_type);
  }
  if (strcmp(opcode, "alloc") == 0)
  {
    return parse_instr_alloca(p, name_hint, result_type);
  }
  if (strcmp(opcode, "load") == 0)
  {
    return parse_instr_load(p, name_hint, result_type);
  }
  if (strcmp(opcode, "store") == 0)
  {
    return parse_instr_store(p);
  }
  if (strcmp(opcode, "gep") == 0)
  {
    return parse_instr_gep(p, name_hint, result_type);
  }
  if (strcmp(opcode, "phi") == 0)
  {
    // PHI 比较特殊, 它需要 result_token 来进行早期注册
    return parse_instr_phi(p, result_token, result_type);
  }
  if (strcmp(opcode, "call") == 0)
  {
    return parse_instr_call(p, name_hint, result_type);
  }

  // 默认
  parser_error(p, "Unknown instruction opcode");
  return NULL;
}

/**
 * @brief [新] 解析 'ret'
 * 语法: ret <operand> | ret void
 */
static IRValueNode *
parse_instr_ret(Parser *p)
{
  // 检查 'ret void'
  if (current_token(p)->type == TK_IDENT && strcmp(current_token(p)->as.ident_val, "void") == 0)
  {
    advance(p); // 消耗 'void'
    if (p->current_function->return_type->kind != IR_TYPE_VOID)
    {
      parser_error(p, "Return type mismatch: expected 'void'");
      return NULL;
    }
    return ir_builder_create_ret(p->builder, NULL);
  }

  // 否则, 期待 'ret %val: type'
  IRValueNode *ret_val = parse_operand(p); // [!!] 使用新版
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

  // 检查 无条件跳转: br $dest
  if (tok.type == TK_LABEL_IDENT)
  {
    IRValueNode *dest = parse_operand(p); // (新版会正确处理 $label)
    if (!dest)
      return NULL;
    return ir_builder_create_br(p->builder, dest);
  }

  // 检查 有条件跳转: br %cond: i1, ...
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

/**
 * @brief [新] 解析 'add'
 * 语法: %res: type = add %lhs: type, %rhs: type
 */
static IRValueNode *
parse_instr_add(Parser *p, const char *name_hint, IRType *result_type)
{
  if (!result_type)
  {
    parser_error(p, "'add' instruction must produce a result");
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
    parser_error(p, "Operands types must match result type for 'add'");
    return NULL;
  }

  return ir_builder_create_add(p->builder, lhs, rhs, name_hint);
}

// --- [!!] 你的任务：实现这些函数的存根 (Stubs) [!!] ---
// (它们都遵循与 parse_instr_add 相同的模式)

static IRValueNode *
parse_instr_sub(Parser *p, const char *name_hint, IRType *result_type)
{
  if (!result_type)
  {
    parser_error(p, "'sub' instruction must produce a result");
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
    parser_error(p, "Operands types must match result type for 'sub'");
    return NULL;
  }
  return ir_builder_create_sub(p->builder, lhs, rhs, name_hint);
}

static IRValueNode *
parse_instr_icmp(Parser *p, const char *name_hint, IRType *result_type)
{
  if (!result_type || result_type->kind != IR_TYPE_I1)
  {
    parser_error(p, "'icmp' must produce an 'i1' result");
    return NULL;
  }

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

// --- [!!] 尚未修复的函数 (它们仍使用"设计 A") [!!] ---
// --- [!!] 我们将在下一步修复它们 (gep/phi/call/load/store) [!!] ---

static IRValueNode *
parse_instr_alloca(Parser *p, const char *name_hint, IRType *result_type)
{
  // 语法: alloca <type>
  IRType *alloc_type = parse_type(p);
  if (!alloc_type)
    return NULL;
  // 验证: %res: <type> = alloca <type>
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
  // 语法: load %ptr: <type>
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
  // 语法: store %val: type, %ptr: <type>
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

  // 1. 检查可选的 'inbounds'
  bool inbounds = false;
  if (current_token(p)->type == TK_IDENT && strcmp(current_token(p)->as.ident_val, "inbounds") == 0)
  {
    inbounds = true;
    advance(p); // 消耗 'inbounds'
  }

  // 2. 解析 %base: ptr
  IRValueNode *base_ptr = parse_operand(p);
  if (!base_ptr || base_ptr->type->kind != IR_TYPE_PTR)
  {
    parser_error(p, "gep base operand must be a pointer (%ptr: <type>)");
    return NULL;
  }
  // 从 base_ptr 推断源类型 (e.g., i32 from <i32>)
  IRType *source_type = base_ptr->type->as.pointee_type;

  // 3. 解析索引列表
  bump_reset(&p->temp_arena);
  size_t capacity = 8;
  size_t count = 0;
  IRValueNode **indices = BUMP_ALLOC_SLICE(&p->temp_arena, IRValueNode *, capacity);
  if (!indices && capacity > 0)
  {
    parser_error(p, "OOM for GEP indices");
    return NULL;
  }

  while (match(p, TK_COMMA))
  {
    // ... (扩容逻辑) ...

    // 3a. 解析 %idx: type
    IRValueNode *idx_val = parse_operand(p);
    if (!idx_val)
    {
      parser_error(p, "Expected GEP index operand");
      return NULL;
    }
    // 3b. 验证索引必须是整数
    if (idx_val->type->kind < IR_TYPE_I1 || idx_val->type->kind > IR_TYPE_I64)
    {
      parser_error(p, "GEP indices must be integer types");
      return NULL;
    }
    indices[count++] = idx_val;
  }

  if (count == 0)
  {
    parser_error(p, "gep must have at least one index operand");
    return NULL;
  }

  return ir_builder_create_gep(p->builder, source_type, base_ptr, indices, count, inbounds, name_hint);
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

  // [!!] PHI 节点必须在解析其传入值 *之前* 注册
  // 因为它的传入值可能是对它自己的前向引用 (循环)
  if (result_token)
  {
    parser_record_value(p, result_token, phi_node);
  }

  // 检查是否至少有一个传入块
  if (current_token(p)->type != TK_LBRACKET)
  {
    parser_error(p, "phi instruction must have at least one incoming value");
    return NULL;
  }

  // 解析 [ val: type, $bb ], [ val: type, $bb ] ...
  while (true)
  {
    if (!expect(p, TK_LBRACKET))
      goto phi_error;

    // 1. 解析 val: type
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

    // 2. 解析 $bb
    IRValueNode *bb_val = parse_operand(p);
    if (!bb_val || bb_val->kind != IR_KIND_BASIC_BLOCK)
    {
      parser_error(p, "Expected incoming basic block label ($name) in PHI node");
      goto phi_error;
    }
    IRBasicBlock *bb = container_of(bb_val, IRBasicBlock, label_address);

    // 3. 添加
    ir_phi_add_incoming(phi_node, val, bb);

    if (!expect(p, TK_RBRACKET))
      goto phi_error;

    if (match(p, TK_COMMA) == false)
      break; // 结束循环
  }
  return phi_node;

phi_error:
  // (确保我们至少报告一个通用错误)
  parser_error(p, "Failed to parse PHI node incoming values");
  return NULL;
}

/**
 * @brief [半重构] 解析 'call'
 *
 * 语法 (Bug B 仍在):
 * %res: type = call <func_ptr_type> %callee( %arg1: type1, ... )
 */
static IRValueNode *
parse_instr_call(Parser *p, const char *name_hint, IRType *result_type)
{
  // 1. [!!] 这是 Bug B 的核心 [!!]
  // Parser 期望 <func_ptr_type>
  // Printer *不* 打印它
  // 我们暂时保留 Parser 的期望, 稍后修复 Printer
  IRType *func_ptr_type = parse_type(p);
  if (!func_ptr_type || func_ptr_type->kind != IR_TYPE_PTR || func_ptr_type->as.pointee_type->kind != IR_TYPE_FUNCTION)
  {
    parser_error(p, "Expected pointer-to-function type (e.g., '<i32(i32)>') before callee");
    return NULL;
  }
  IRType *func_type = func_ptr_type->as.pointee_type;

  // 1a. 验证返回类型
  if (func_type->as.function.return_type != result_type)
  {
    parser_error(p, "Call result type annotation does not match function's return type");
    return NULL;
  }

  // 2. [!!] 修复: 使用旧的 parse_operand(p, type)
  // 因为我们的新 parse_operand 期望 %val: type,
  // 但这里的 callee 语法是 <type> %val
  // [!!]
  // [!!] 让我们修复这个问题！
  // 我们将手动解析 callee
  Token callee_tok = *current_token(p);
  IRValueNode *callee_val = NULL;
  if (callee_tok.type == TK_LOCAL_IDENT || callee_tok.type == TK_GLOBAL_IDENT)
  {
    advance(p); // 消耗 %callee
    callee_val = parser_find_value(p, &callee_tok);
    if (!callee_val)
      return NULL;
    if (callee_val->type != func_ptr_type)
    {
      parser_error(p, "Callee's type does not match explicit function pointer type");
      return NULL;
    }
  }
  else
  {
    parser_error(p, "Expected callee name (%func_ptr or @func) after type");
    return NULL;
  }

  // 3. 解析参数列表 `( %arg1: type1, ... )`
  if (!expect(p, TK_LPAREN))
    return NULL;

  bump_reset(&p->temp_arena);
  size_t capacity = 8;
  size_t count = 0;
  IRValueNode **arg_values = BUMP_ALLOC_SLICE(&p->temp_arena, IRValueNode *, capacity);
  // ... (OOM 检查) ...

  bool is_variadic = func_type->as.function.is_variadic;
  size_t expected_count = func_type->as.function.param_count;

  if (current_token(p)->type != TK_RPAREN)
  {
    while (true)
    {
      // ... (扩容逻辑) ...
      if (match(p, TK_ELLIPSIS))
      { /* ... '...' 逻辑 ... */
      }

      // 4a. [!! 已重构 !!] 解析 %arg: type
      IRValueNode *arg_val = parse_operand(p);
      if (!arg_val)
        return NULL;
      IRType *arg_type = arg_val->type;

      // 4b. 验证类型
      if (!is_variadic && count >= expected_count)
      {
        parser_error(p, "Too many arguments");
        return NULL;
      }
      if (count < expected_count)
      {
        if (arg_type != func_type->as.function.param_types[count])
        {
          parser_error(p, "Argument type mismatch in call");
          return NULL;
        }
      }
      arg_values[count++] = arg_val;

      if (match(p, TK_RPAREN))
        break;
      if (!expect(p, TK_COMMA))
        return NULL;
    }
  }
  else
  {
    advance(p); // 消耗 ')'
  }

  // 5. 验证数量
  // ... (is_variadic 检查) ...

  return ir_builder_create_call(p->builder, callee_val, arg_values, count, name_hint);
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
  // (调用者已经消耗了 '[')
  Token *count_tok = current_token(p);
  if (!expect(p, TK_INTEGER_LITERAL))
  {
    return NULL; // 错误: 缺少数组大小
  }
  size_t count = (size_t)count_tok->as.int_val;

  if (count_tok->as.int_val < 0)
  {
    parser_error(p, "Array size cannot be negative");
    return NULL;
  }

  if (!expect_ident(p, "x"))
  {
    return NULL; // 错误: 缺少 'x'
  }

  IRType *element_type = parse_type(p);
  if (!element_type)
  {
    return NULL; // 错误: 缺少元素类型
  }

  if (!expect(p, TK_RBRACKET))
  {
    return NULL; // 错误: 缺少 ']'
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
  // (调用者已经消耗了 '{')

  // 1. 重置临时分配器，用于构建成员列表
  bump_reset(&p->temp_arena);
  size_t capacity = 8;
  size_t count = 0;
  IRType **members = BUMP_ALLOC_SLICE(&p->temp_arena, IRType *, capacity);
  if (!members)
  {
    parser_error(p, "OOM in temp_arena for struct members");
    return NULL;
  }

  // 检查空结构体: {}
  if (current_token(p)->type == TK_RBRACE)
  {
    advance(p); // 消耗 '}'
    return ir_type_get_anonymous_struct(p->context, NULL, 0);
  }

  // 2. 循环解析成员类型
  while (true)
  {
    // 检查是否需要扩容
    if (count == capacity)
    {
      size_t new_capacity = capacity * 2;
      IRType **new_members = BUMP_ALLOC_SLICE(&p->temp_arena, IRType *, new_capacity);
      if (!new_members)
      {
        parser_error(p, "OOM in temp_arena resizing struct members");
        return NULL;
      }
      memcpy(new_members, members, capacity * sizeof(IRType *));
      members = new_members;
      capacity = new_capacity;
    }

    // 解析一个成员
    IRType *member_type = parse_type(p);
    if (!member_type)
    {
      return NULL; // 错误: 缺少成员类型
    }
    members[count++] = member_type;

    // 检查结束: '}'
    if (match(p, TK_RBRACE))
    {
      break;
    }

    // 否则，必须是逗号: ','
    if (!expect(p, TK_COMMA))
    {
      return NULL; // 错误: 缺少 ',' 或 '}'
    }
  }

  // 3. 将最终的成员列表复制到 *永久* Arena
  // (因为类型是永久的，但我们的列表是在 temp_arena 中的)
  IRType **permanent_members = BUMP_ALLOC_SLICE_COPY(&p->context->permanent_arena, IRType *, members, count);
  if (!permanent_members)
  {
    parser_error(p, "OOM in permanent_arena copying struct members");
    return NULL;
  }

  // 4. 获取（或创建）唯一的匿名结构体类型
  return ir_type_get_anonymous_struct(p->context, permanent_members, count);
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
  Token *tok = current_token(p);
  IRType *base_type = NULL;

  switch (tok->type)
  {
  case TK_LT: {
    advance(p); // 消耗 '<'
    IRType *pointee_type = parse_type(p);
    if (!pointee_type)
      return NULL;
    if (!expect(p, TK_GT))
      return NULL; // 消耗 '>'
    base_type = ir_type_get_ptr(p->context, pointee_type);
    break;
  }
  case TK_IDENT: {
    const char *name = tok->as.ident_val;
    // ... (处理 'void', 'i32', 'i64', 'f32', 'f64' ... 的代码) ...
    if (strcmp(name, "void") == 0)
    {
      base_type = ir_type_get_void(p->context);
    }
    else if (strcmp(name, "i1") == 0)
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
      char error_msg[256];
      snprintf(error_msg, sizeof(error_msg), "Unknown type identifier '%s'", name);
      parser_error(p, error_msg);
      return NULL;
    }
    advance(p); // 消耗类型标识符 (e.g., 'i32')
    break;
  }

  case TK_LBRACKET: // [
    advance(p);     // 消耗 '['
    base_type = parse_array_type(p);
    break;

  case TK_LBRACE:                     // {
    advance(p);                       // 消耗 '{'
    base_type = parse_struct_type(p); // (用于 *匿名* 结构体)
    break;

  case TK_LOCAL_IDENT: {
    // 语法 e.g., %my_struct
    Token name_tok = *current_token(p);
    advance(p); // 消耗 %my_struct

    const char *name = name_tok.as.ident_val;
    // [!! 关键 !!] 您的 lexer/context 保证 ident_val 是
    // NUL 结尾的, 我们可以安全使用 strlen。
    size_t name_len = strlen(name);

    // [!!] 直接查询 `context.c` (第 121 行) 公开的缓存
    IRType *found_type = (IRType *)str_hashmap_get(p->context->named_struct_cache, name, name_len);

    if (found_type == NULL)
    {
      // [!!] 错误：类型未定义
      char error_msg[256];
      snprintf(error_msg, sizeof(error_msg), "Use of undefined named type '%%%s'", name_tok.as.ident_val);
      parser_error(p, error_msg);
      return NULL;
    }
    base_type = found_type;
    break;
  }

  default:
    parser_error(p, "Expected a type signature");
    return NULL;
  }
  // 检查函数类型
  // 我们刚刚解析了一个 'base_type' (e.g., "i32")
  // 我们现在必须检查后面是否跟了 '('，
  // 如果是，说明这是一个函数类型 (e.g., "i32 (i32)")
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
    // 根据期望的类型 kind，调用正确的 Context API
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
      parser_error(p, "Integer literal provided for non-integer type");
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
      parser_error(p, "Float literal provided for non-float type");
      return NULL;
    }
  }

  case TK_IDENT: {
    const char *name = val_tok->as.ident_val;
    if (strcmp(name, "true") == 0)
    {
      if (type->kind != IR_TYPE_I1)
        parser_error(p, "'true' must have type 'i1'");
      return ir_constant_get_i1(p->context, true);
    }
    if (strcmp(name, "false") == 0)
    {
      if (type->kind != IR_TYPE_I1)
        parser_error(p, "'false' must have type 'i1'");
      return ir_constant_get_i1(p->context, false);
    }
    if (strcmp(name, "undef") == 0)
    {
      return ir_constant_get_undef(p->context, type);
    }
    if (strcmp(name, "null") == 0)
    {
      if (type->kind != IR_TYPE_PTR)
        parser_error(p, "'null' must have 'ptr' type");
      return ir_constant_get_undef(p->context, type);
    }
    parser_error(p, "Unexpected identifier as constant value");
    return NULL;
  }
  default:
    parser_error(p, "Unexpected token as constant value");
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
  advance(p); // 立即消耗值 (e.g., %a, 10, $label)

  // --- 特殊情况: $label, 它们没有 ': type' ---
  if (val_tok.type == TK_LABEL_IDENT)
  {
    const char *label_name = val_tok.as.ident_val;
    IRValueNode *val = (IRValueNode *)ptr_hashmap_get(p->local_value_map, (void *)label_name);
    if (!val)
    {
      // 前向声明
      IRBasicBlock *fwd_bb = ir_basic_block_create(p->current_function, label_name);
      val = (IRValueNode *)&fwd_bb->label_address;
      ptr_hashmap_put(p->local_value_map, (void *)label_name, (void *)val);
    }
    if (val->kind != IR_KIND_BASIC_BLOCK)
    {
      parser_error(p, "Expected a basic block label ($name)");
      return NULL;
    }
    return val;
  }

  // --- 所有其他值都必须遵循 %val: type 或 const: type ---

  if (!expect(p, TK_COLON))
  {
    parser_error(p, "Expected ':' after operand value");
    return NULL;
  }

  IRType *type = parse_type(p);
  if (!type)
    return NULL;

  // 根据我们之前消耗的 Token 类型进行分派
  switch (val_tok.type)
  {
  // --- 1. 变量 (来自符号表) ---
  case TK_LOCAL_IDENT:
  case TK_GLOBAL_IDENT: {
    IRValueNode *val = parser_find_value(p, &val_tok);
    if (!val)
      return NULL; // find_value 已报告错误
    if (val->type != type)
    {
      // [!!] 关键验证
      parser_error(p, "Variable's type annotation does not match its definition type");
      return NULL;
    }
    return val;
  }

  // --- 2. 常量 (需要创建) ---
  case TK_INTEGER_LITERAL:
  case TK_FLOAT_LITERAL:
  case TK_IDENT: // (true, false, undef, null)
  {
    return parse_constant_from_token(p, &val_tok, type);
  }

  default:
    parser_error(p, "Unexpected token as operand value");
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

  // 1. 初始化 Lexer
  Lexer lexer;
  ir_lexer_init(&lexer, source_buffer, ctx);

  // 2. 初始化 Builder (解析期间共享)
  IRBuilder *builder = ir_builder_create(ctx);
  if (!builder)
  {
    fprintf(stderr, "Fatal: Failed to create IRBuilder\n");
    return NULL;
  }

  // 3. 解析可选的模块名
  const char *module_name = "parsed_module"; // 默认名
  const Token *first_tok = ir_lexer_current_token(&lexer);

  // 检查是否为 'source_filename = "..."'
  if (first_tok->type == TK_IDENT && strcmp(first_tok->as.ident_val, "module") == 0)
  {
    ir_lexer_next(&lexer); // 消耗 'source_filename'

    // 检查 '='
    if (ir_lexer_current_token(&lexer)->type != TK_EQ)
    {
      fprintf(stderr, "Parse Error (Line %zu): Expected '=' after 'module'\n", ir_lexer_current_token(&lexer)->line);
      ir_builder_destroy(builder);
      return NULL;
    }
    ir_lexer_next(&lexer); // 消耗 '='

    // 检查 "..." (字符串字面量)
    const Token *name_tok = ir_lexer_current_token(&lexer);
    if (name_tok->type != TK_STRING_LITERAL)
    {
      fprintf(stderr, "Parse Error (Line %zu): Expected string literal (e.g., \"foo.c\") after 'module ='\n",
              name_tok->line);
      ir_builder_destroy(builder);
      return NULL;
    }

    // [!!] 获取 interned 字符串
    module_name = name_tok->as.ident_val;
    ir_lexer_next(&lexer); // 消耗字符串
  }

  // 4. 创建目标 Module (使用解析到的名字)
  IRModule *module = ir_module_create(ctx, module_name);
  if (!module)
  {
    ir_builder_destroy(builder);
    fprintf(stderr, "Fatal: Failed to create IRModule\n");
    return NULL;
  }

  // 5. 初始化 Parser
  Parser parser;
  if (!parser_init(&parser, &lexer, ctx, module, builder))
  {
    ir_builder_destroy(builder);
    fprintf(stderr, "Fatal: Failed to init Parser (OOM)\n");
    // 模块已经在 ir_arena 中，让 context 清理
    return NULL;
  }

  // 6. 运行主解析循环
  parse_module_body(&parser);

  // 7. 检查错误
  bool success = !parser.has_error;

  // 8. 清理
  parser_destroy(&parser);
  ir_builder_destroy(builder);

  if (success)
  {
    // [已更新] 运行 Verifier 验证解析出的 IR
    if (!ir_verify_module(module))
    {
      fprintf(stderr, "Parser Error: Generated IR failed verification.\n");
      // 验证失败，返回 NULL
      return NULL;
    }
    return module;
  }
  else
  {
    // 解析失败。模块可能处于半成品状态。
    // ir_arena 中残留的数据将由 context 稍后清理。
    return NULL;
  }
}
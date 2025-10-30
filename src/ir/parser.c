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
static IRValueNode *parse_instruction(Parser *p, bool *out_is_terminator);
static IRValueNode *parse_operation(Parser *p, Token *result_token, bool *out_is_terminator);
static IRType *parse_type(Parser *p);
static IRValueNode *parse_operand(Parser *p, IRType *expected_type);
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
    else if (strcmp(tok->as.ident_val, "type") == 0)
    {
      advance(p); // 消耗 'type'
      parse_type_definition(p);
    }
    else if (strcmp(tok->as.ident_val, "source_filename") == 0)
    {
      parser_error(p, "'source_filename' directive must appear at the top of the module");
      advance(p); // 消耗 'source_filename'
    }
    else
    {
      parser_error(p, "Expected 'define', 'declare', or 'type' at top level");
      advance(p); // 消耗错误 token
    }
    break;

  case TK_GLOBAL_IDENT:
    // @gvar = ...
    parse_global_variable(p);
    break;

  default:
    parser_error(p, "Unexpected token at top level");
    advance(p); // 消耗错误 token
    break;
  }
}

/**
 * @brief 解析一个函数定义 (带函数体)
 *
 * 语法: `define <ret_type> @<name> ( <arg_list> ) { ... }`
 *
 * @param p Parser (当前 token 是 'define')
 */
static void
parse_function_definition(Parser *p)
{
  // 1. 消耗 'define'
  advance(p); // 消耗 'define'

  // 2. 解析返回类型
  IRType *ret_type = parse_type(p);
  if (!ret_type)
    return;

  // 3. 解析 @name
  Token name_tok = *current_token(p);
  if (!expect(p, TK_GLOBAL_IDENT))
    return;

  // 4. 解析参数列表 `(type %name, ...)`
  // 我们将参数存储在 temp_arena 中
  bump_reset(&p->temp_arena);
  size_t arg_capacity = 8;
  size_t arg_count = 0;
  ParsedArgument *arg_list = BUMP_ALLOC_SLICE(&p->temp_arena, ParsedArgument, arg_capacity);
  if (!arg_list)
  {
    parser_error(p, "OOM in temp_arena for function arguments");
    return;
  }

  if (!expect(p, TK_LPAREN))
    return;

  // 循环解析: `type %name`
  if (current_token(p)->type != TK_RPAREN)
  {
    while (true)
    {
      // 扩容
      if (arg_count == arg_capacity)
      {
        size_t new_capacity = arg_capacity * 2;
        ParsedArgument *new_list = BUMP_ALLOC_SLICE(&p->temp_arena, ParsedArgument, new_capacity);
        if (!new_list)
        {
          parser_error(p, "OOM resizing argument list");
          return;
        }
        memcpy(new_list, arg_list, arg_capacity * sizeof(ParsedArgument));
        arg_list = new_list;
        arg_capacity = new_capacity;
      }

      // 解析 `type %name`
      IRType *arg_type = parse_type(p);
      if (!arg_type)
        return;
      Token arg_name_tok = *current_token(p);
      if (!expect(p, TK_LOCAL_IDENT))
        return;

      arg_list[arg_count].type = arg_type;
      arg_list[arg_count].name_tok = arg_name_tok;
      arg_count++;

      // 检查 ')' 或 ','
      if (match(p, TK_RPAREN))
      {
        break;
      }
      if (!expect(p, TK_COMMA))
      {
        return;
      }
    }
  }
  else
  {
    advance(p); // 消耗 ')' (空参数列表)
  }

  // 5. 查找 '{'
  if (!expect(p, TK_LBRACE))
  {
    return;
  }

  // --- 关键步骤: 现在我们拥有了签名，开始构建 IR 对象 ---

  // 6. 创建 IRFunction
  IRFunction *func = ir_function_create(p->module, name_tok.as.ident_val, ret_type);
  if (!func)
  {
    parser_error(p, "Failed to create function object (OOM?)");
    return;
  }

  // 7. 将函数注册到*全局*符号表
  // (我们使用 entry_address 作为函数的值)
  parser_record_value(p, &name_tok, (IRValueNode *)&func->entry_address);

  // 8. [进入函数作用域]
  p->current_function = func;
  // (temp_arena 此时包含 arg_list, 我们不能重置它)
  p->local_value_map = ptr_hashmap_create(&p->temp_arena, 64);
  if (!p->local_value_map)
  {
    parser_error(p, "OOM creating local value map");
    return;
  }

  // 9. 创建函数类型 (IRFunctionType) 并设置参数
  // (我们需要一个新的、紧凑的 IRType* 数组)
  IRType **arg_types_array = BUMP_ALLOC_SLICE(&p->temp_arena, IRType *, arg_count);
  if (arg_count > 0 && !arg_types_array)
  {
    parser_error(p, "OOM creating argument type array");
    return;
  }
  for (size_t i = 0; i < arg_count; i++)
  {
    arg_types_array[i] = arg_list[i].type;
  }

  // 9. 创建 IRArgument 对象并注册到*局部*符号表
  for (size_t i = 0; i < arg_count; i++)
  {
    IRArgument *arg = ir_argument_create(func, arg_list[i].type, arg_list[i].name_tok.as.ident_val);
    if (!arg)
    {
      parser_error(p, "Failed to create argument object");
      return;
    }
    // (我们使用 argument->value 作为参数的值)
    parser_record_value(p, &arg_list[i].name_tok, (IRValueNode *)&arg->value);
  }

  // 10. 解析函数体 (基本块)
  while (current_token(p)->type != TK_RBRACE && current_token(p)->type != TK_EOF)
  {
    if (p->has_error)
      break;

    // 基本块必须以 'name:' (TK_IDENT + TK_COLON) 开头
    if (current_token(p)->type == TK_IDENT && ir_lexer_peek_token(p->lexer)->type == TK_COLON)
    {
      parse_basic_block(p);
    }
    else
    {
      // [!!] 更新了错误信息
      parser_error(p, "Expected basic block label (e.g., entry:)");
      break;
    }
  }

  // 11. 查找 '}'
  if (!expect(p, TK_RBRACE))
  {
    return;
  }

  // 12. [退出函数作用域]
  p->current_function = NULL;
  p->local_value_map = NULL;
  // 销毁此函数的所有临时数据 (arg_list, local_value_map)
  bump_reset(&p->temp_arena);
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
  // 1. 消耗 'declare'
  advance(p); // 消耗 'declare'

  // 2. 解析返回类型
  IRType *ret_type = parse_type(p);
  if (!ret_type)
    return;

  // 3. 解析 @name
  Token name_tok = *current_token(p);
  if (!expect(p, TK_GLOBAL_IDENT))
    return;

  // --- 关键步骤: 立即创建 IR 对象 ---
  // (ir_function_create 已设置 .type = ret_type)
  IRFunction *func = ir_function_create(p->module, name_tok.as.ident_val, ret_type);
  if (!func)
  {
    parser_error(p, "Failed to create function object (OOM?)");
    return;
  }
  // 将其注册到*全局*符号表
  parser_record_value(p, &name_tok, (IRValueNode *)&func->entry_address);

  // 4. 解析参数列表 `(type, ...)`
  // 我们必须创建 IRArgument 对象
  // (即使没有名字), 这样 'call' 和 'dump' 才能工作。

  if (!expect(p, TK_LPAREN))
    return;

  if (current_token(p)->type != TK_RPAREN)
  {
    while (true)
    {
      IRType *arg_type = parse_type(p);
      if (!arg_type)
        return;

      if (current_token(p)->type == TK_LOCAL_IDENT)
      {
        parser_error(p, "Argument names (e.g., %arg) are not allowed in 'declare' signatures");
        return;
      }

      // 创建 IRArgument (名字为 NULL)
      if (!ir_argument_create(func, arg_type, NULL))
      {
        parser_error(p, "Failed to create argument for declaration");
        return;
      }

      if (match(p, TK_RPAREN))
      {
        break;
      }
      if (!expect(p, TK_COMMA))
      {
        return;
      }
    }
  }
  else
  {
    advance(p); // 消耗 ')' (空参数列表)
  }

  // 5. 清理 temp_arena
  // (我们没有用它, 但以防 parse_type 用了)
  bump_reset(&p->temp_arena);
}

/*
 * -----------------------------------------------------------------
 * --- 函数体解析 (Function Body Parsing) ---
 * -----------------------------------------------------------------
 */

/**
 * @brief 解析一个命名类型 (结构体) 定义
 *
 * 语法: `type %<name> = { <type_list> }`
 * e.g., `type %my_struct = { i32, ptr i8 }`
 *
 * @param p Parser (当前 token 是 'type')
 */
static void
parse_type_definition(Parser *p)
{
  // 1. 解析 %name
  Token name_tok = *current_token(p);
  if (!expect(p, TK_LOCAL_IDENT))
  {
    parser_error(p, "Expected named type (e.g., %my_struct) after 'type'");
    return;
  }
  // [!!] 我们需要 interned C 字符串, Lexer 已经提供了
  const char *name = name_tok.as.ident_val;

  // 2. 解析 =
  if (!expect(p, TK_EQ))
    return;

  // 3. 解析结构体字面量 { ... }
  if (!expect(p, TK_LBRACE))
  {
    parser_error(p, "Expected struct body '{...}' or 'opaque' after '='");
    return;
  }

  // 4. 解析成员列表 (与 parse_struct_type 逻辑相同)
  // 4a. 重置临时分配器
  bump_reset(&p->temp_arena);
  size_t capacity = 8;
  size_t count = 0;
  IRType **members = BUMP_ALLOC_SLICE(&p->temp_arena, IRType *, capacity);
  if (!members)
  {
    parser_error(p, "OOM in temp_arena for struct members");
    return;
  }

  // 检查空结构体: {}
  if (current_token(p)->type == TK_RBRACE)
  {
    advance(p); // 消耗 '}'
  }
  else
  {
    // 4b. 循环解析成员类型
    while (true)
    {
      if (count == capacity)
      {
        // [!!] 实现了扩容逻辑
        size_t new_capacity = capacity * 2;
        IRType **new_members = BUMP_ALLOC_SLICE(&p->temp_arena, IRType *, new_capacity);
        if (!new_members)
        {
          parser_error(p, "OOM in temp_arena resizing struct members");
          return;
        }
        memcpy(new_members, members, capacity * sizeof(IRType *));
        members = new_members;
        capacity = new_capacity;
      }

      IRType *member_type = parse_type(p);
      if (!member_type)
        return;
      members[count++] = member_type;

      if (match(p, TK_RBRACE))
        break; // 结束
      if (!expect(p, TK_COMMA))
        return; // 错误: 缺少 ',' 或 '}'
    }
  }

  // 4c. 将最终的成员列表复制到 *永久* Arena
  // (因为 `ir_type_create_struct` 需要一个永久的列表)
  IRType **permanent_members = BUMP_ALLOC_SLICE_COPY(&p->context->permanent_arena, IRType *, members, count);
  if (!permanent_members)
  {
    parser_error(p, "OOM in permanent_arena copying struct members");
    return;
  }

  // 5. [!!] 调用正确的 API (get-or-create)
  IRType *named_struct = ir_type_get_named_struct(p->context, name, permanent_members, count);

  if (named_struct == NULL)
  {
    // 您的 context.c 实现会 assert(0) (第 338 行)
    // 但如果它失败 (例如 OOM), 我们也处理
    parser_error(p, "Failed to create or register named struct (redefinition with different shape?)");
    return;
  }

  // 注意: 类型不需要注册到 parser->global_value_map, 因为它们不是 "值"
}

/**
 * @brief 解析一个全局变量定义
 *
 * 语法: `@<name> = 'global' <type> <constant_initializer>`
 * e.g., `@gvar = global i32 5`
 *
 * @param p Parser (当前 token 是 TK_GLOBAL_IDENT)
 */
static void
parse_global_variable(Parser *p)
{
  // 1. 解析 @name
  // 我们需要保存 Token 本身，以便稍后用于 'parser_record_value' (它需要原始 token)
  Token name_tok = *current_token(p);
  if (!expect(p, TK_GLOBAL_IDENT))
  {
    return; // 错误: 已经报告
  }

  // 2. 解析 =
  if (!expect(p, TK_EQ))
  {
    return; // 错误: 已经报告
  }

  // 3. 解析 'global' 关键字
  if (!expect_ident(p, "global"))
  {
    return; // 错误: 已经报告
  }

  // 4. 解析全局变量的*类型*
  // (e.g., 'i32' in '@gvar = global i32 5')
  IRType *allocated_type = parse_type(p);
  if (allocated_type == NULL)
  {
    return; // 错误: 由 parse_type 报告
  }

  // 5. [!! 已修复 !!] 解析*初始值*
  IRValueNode *initializer = NULL;
  Token *val_tok = current_token(p);

  // 5a. 检查 'zeroinitializer' (这是一个 TK_IDENT)
  if (val_tok->type == TK_IDENT && strcmp(val_tok->as.ident_val, "zeroinitializer") == 0)
  {
    advance(p); // 消耗 'zeroinitializer'
    // 'initializer' 保持为 NULL
    // ir_global_variable_create API 将处理 NULL -> 零初始化
  }
  // 5b. 否则, 解析一个具体的值 (e.g., 123, true, undef)
  else
  {
    // [!!] 我们调用 parse_operand, 它被告知期望的类型 (allocated_type)
    initializer = parse_operand(p, allocated_type);
    if (initializer == NULL)
    {
      parser_error(p, "Expected initializer (e.g., '123' or 'zeroinitializer') after global type");
      return; // 错误已由 parse_operand 报告
    }
  }

  // 7. 创建 IRGlobalVariable 对象
  // 你的 API `ir_global_variable_create` 已经处理了:
  // - 在 ir_arena 中分配
  // - 设置父模块
  // - 添加到模块的 globals 链表
  IRGlobalVariable *gvar = ir_global_variable_create(p->module,
                                                     name_tok.as.ident_val, // 来自 Lexer 的 interned 字符串
                                                     allocated_type, initializer);

  if (gvar == NULL)
  {
    parser_error(p, "Failed to create global variable object (OOM?)");
    return;
  }

  // 8. 将新创建的全局变量注册到*全局符号表*
  // 这样后续的 @main (或其他函数) 就可以通过 '@gvar' 引用它
  // (我们传入原始的 name_tok 来进行重定义检查)
  parser_record_value(p, &name_tok, (IRValueNode *)gvar);
}

/**
 * @brief 解析一个基本块
 *
 * 语法: `<label>: instruction*`
 * e.g., `entry: ret void`
 *
 * @param p Parser (当前 token 是 TK_LOCAL_IDENT)
 */
static void
parse_basic_block(Parser *p)
{
  if (!p->current_function)
  {
    parser_error(p, "Basic block definition found outside of a function");
    return;
  }

  // 1. 解析标签 (name:)
  Token name_tok = *current_token(p);
  if (!expect(p, TK_IDENT))
  {
    parser_error(p, "Expected basic block label name (e.g., 'entry')");
    return;
  }
  if (!expect(p, TK_COLON))
    return;

  const char *name = name_tok.as.ident_val; // "entry"
  // 2. 创建 BasicBlock
  IRBasicBlock *bb = NULL;
  IRValueNode *existing_val = (IRValueNode *)ptr_hashmap_get(p->local_value_map, (void *)name);

  if (existing_val) // [!!] 之前被前向声明过?
  {
    if (existing_val->kind != IR_KIND_BASIC_BLOCK)
    {
      parser_error(p, "Label name conflicts with an existing value");
      return;
    }
    bb = container_of(existing_val, IRBasicBlock, label_address);

    // [!!] 检查重定义: 如果 list_node 不是 unlinked, 说明它已被附加过
    if (bb->list_node.next != &bb->list_node)
    {
      char error_msg[256];
      snprintf(error_msg, sizeof(error_msg), "Redefinition of basic block label '%s'", name);
      parser_error(p, error_msg);
      return;
    }
  }
  else // [!!] 第一次遇到这个标签
  {
    bb = ir_basic_block_create(p->current_function, name);
    if (!bb)
    { /* ... OOM 错误 ... */
    }

    // 注册到符号表
    ptr_hashmap_put(p->local_value_map, (void *)name, (void *)&bb->label_address);
  }

  // 3. [!!] 将块附加到函数 (现在才做!)
  ir_function_append_basic_block(p->current_function, bb);

  // 4. 设置 Builder 的插入点
  ir_builder_set_insertion_point(p->builder, bb);

  // 5. 循环解析指令
  while (true)
  {
    if (p->has_error)
      return;

    Token *tok = current_token(p);

    // 检查函数结束
    if (tok->type == TK_RBRACE)
    {
      return;
    }

    // 检查是否为下一个基本块的标签
    if (tok->type == TK_IDENT && ir_lexer_peek_token(p->lexer)->type == TK_COLON)
    {
      return;
    }

    // 解析一条指令
    bool is_terminator = false;
    parse_instruction(p, &is_terminator);

    // 检查该指令是否是终结者
    if (is_terminator)
    {
      // 已经解析了终结者指令 (ret, br)
      // 检查后面是否还有指令
      if (current_token(p)->type != TK_RBRACE && ir_lexer_peek_token(p->lexer)->type != TK_COLON)
      {
        parser_error(p, "Instructions are not allowed after a terminator");
      }
      return; // 停止解析此块
    }
  }
}

/**
 * @brief 解析一条指令 (调度器)
 *
 * @param p Parser
 * @param out_is_terminator [输出] 如果解析的指令是终结者，则设为 true
 * @return IRValueNode* 指向新指令的 Value (如果指令有结果)
 */
static IRValueNode *
parse_instruction(Parser *p, bool *out_is_terminator)
{
  Token tok = *current_token(p);
  Token result_tok;
  bool has_result = false;

  // 检查是否为带结果的指令 (Case 2)
  if (tok.type == TK_LOCAL_IDENT && ir_lexer_peek_token(p->lexer)->type == TK_EQ)
  {
    has_result = true;
    result_tok = tok; // 保存 %name Token
    advance(p);       // 消耗 %name
    advance(p);       // 消耗 =
  }

  // 解析操作 (e.g., 'add i32 %a, %b')
  IRValueNode *instr_val = parse_operation(p, has_result ? &result_tok : NULL, out_is_terminator);

  // 如果有结果，注册到局部符号表
  if (has_result && instr_val)
  {
    // [!!] PHI 指令由 parse_operation *内部* 注册 (为了递归)
    // 其他所有指令都在这里注册。
    // 我们需要向下转型来检查 opcode。
    assert(instr_val->kind == IR_KIND_INSTRUCTION);
    IRInstruction *inst = container_of(instr_val, IRInstruction, result);

    if (inst->opcode != IR_OP_PHI)
    {
      parser_record_value(p, &result_tok, instr_val);
    }
    // 如果是 PHI, parse_operation 已经处理了注册,
    // 我们跳过, 防止 "Redef redefining" 错误。
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
parse_operation(Parser *p, Token *result_token, bool *out_is_terminator)
{
  *out_is_terminator = false;

  Token opcode_tok = *current_token(p);
  if (!expect(p, TK_IDENT))
  {
    parser_error(p, "Expected instruction opcode");
    return NULL;
  }
  const char *opcode = opcode_tok.as.ident_val;

  // --- 根据 Opcode 分派 ---

  // 1. 终结者: ret
  if (strcmp(opcode, "ret") == 0)
  {
    IRType *ret_type = parse_type(p);
    IRValueNode *ret_val = NULL;
    if (ret_type->kind != IR_TYPE_VOID)
    {
      ret_val = parse_operand(p, ret_type);
    }
    *out_is_terminator = true;
    return ir_builder_create_ret(p->builder, ret_val);
  }

  // 2. 终结者: br
  if (strcmp(opcode, "br") == 0)
  {
    *out_is_terminator = true;

    // 检查是 'br label ...' (无条件) 还是 'br <type> ...' (有条件)
    Token *peek_tok = current_token(p);

    if (peek_tok->type == TK_IDENT && strcmp(peek_tok->as.ident_val, "label") == 0)
    {
      // --- 无条件跳转 ---
      // 语法: br label %dest
      IRValueNode *dest = parse_operand(p, p->context->type_label);
      return ir_builder_create_br(p->builder, dest);
    }
    else
    {
      // --- 有条件跳转 ---
      // 语法: br i1 %cond, label %true, label %false

      // 1. 解析 <type> (e.g., "i1")
      IRType *cond_type = parse_type(p);
      if (!cond_type)
      {
        parser_error(p, "Expected type (e.g., 'i1') for 'br' condition");
        return NULL;
      }
      // 1a. 验证它必须是 i1
      if (cond_type->kind != IR_TYPE_I1)
      {
        parser_error(p, "'br' condition type must be 'i1'");
        return NULL;
      }

      // 2. 解析 <cond> (e.g., "%cmp")
      // (现在 parse_operand 拿到的是 %cmp, 这是正确的)
      IRValueNode *cond = parse_operand(p, cond_type);
      if (!cond)
        return NULL;

      // 3. 解析标签
      if (!expect(p, TK_COMMA))
        return NULL;
      IRValueNode *true_dest = parse_operand(p, p->context->type_label);
      if (!expect(p, TK_COMMA))
        return NULL;
      IRValueNode *false_dest = parse_operand(p, p->context->type_label);

      return ir_builder_create_cond_br(p->builder, cond, true_dest, false_dest);
    }
  }

  // 3. 内存: alloca
  if (strcmp(opcode, "alloca") == 0)
  {
    IRType *alloc_type = parse_type(p);
    return ir_builder_create_alloca(p->builder, alloc_type);
  }

  // 4. 内存: load
  if (strcmp(opcode, "load") == 0)
  {
    IRType *result_type = parse_type(p);
    if (!expect(p, TK_COMMA))
      return NULL;
    IRType *ptr_type = parse_type(p);
    IRValueNode *ptr = parse_operand(p, ptr_type);
    return ir_builder_create_load(p->builder, result_type, ptr);
  }

  // 5. 内存: store
  if (strcmp(opcode, "store") == 0)
  {
    IRType *val_type = parse_type(p);
    IRValueNode *val = parse_operand(p, val_type);
    if (!expect(p, TK_COMMA))
      return NULL;
    IRType *ptr_type = parse_type(p);
    IRValueNode *ptr = parse_operand(p, ptr_type);
    return ir_builder_create_store(p->builder, val, ptr);
  }

  // 6. 二元运算: add, sub
  if (strcmp(opcode, "add") == 0)
  {
    IRType *type = parse_type(p);
    IRValueNode *lhs = parse_operand(p, type);
    if (!expect(p, TK_COMMA))
      return NULL;
    IRValueNode *rhs = parse_operand(p, type);
    return ir_builder_create_add(p->builder, lhs, rhs);
  }
  if (strcmp(opcode, "sub") == 0)
  {
    IRType *type = parse_type(p);
    IRValueNode *lhs = parse_operand(p, type);
    if (!expect(p, TK_COMMA))
      return NULL;
    IRValueNode *rhs = parse_operand(p, type);
    return ir_builder_create_sub(p->builder, lhs, rhs);
  }

  // 7. 比较: icmp
  if (strcmp(opcode, "icmp") == 0)
  {
    IRICmpPredicate pred = parse_icmp_predicate(p);
    IRType *type = parse_type(p);
    IRValueNode *lhs = parse_operand(p, type);
    if (!expect(p, TK_COMMA))
      return NULL;
    IRValueNode *rhs = parse_operand(p, type);
    return ir_builder_create_icmp(p->builder, pred, lhs, rhs);
  }

  // 8. PHI 节点
  if (strcmp(opcode, "phi") == 0)
  {
    IRType *type = parse_type(p);
    IRValueNode *phi_node = ir_builder_create_phi(p->builder, type);

    // [关键] 我们必须立即设置 phi 节点的名字 (如果
    // builder API 不支持)，
    // 因为 phi 的操作数可能递归地引用它自己。
    if (result_token)
    {
      // (我们不能调用 parser_record_value, 因为它会检查重定义)
      // (我们必须手动设置名字，并*立即*放入 map)
      const char *name = result_token->as.ident_val;
      ir_value_set_name(phi_node, name);
      ptr_hashmap_put(p->local_value_map, (void *)name, (void *)phi_node);
    }

    // 解析 [ val, %bb ], [ val, %bb ] ...
    while (true)
    {
      if (!expect(p, TK_LBRACKET))
        goto phi_error;
      IRValueNode *val = parse_operand(p, type);
      if (!expect(p, TK_COMMA))
        goto phi_error;

      // [!! 已修复 !!] PHI 的操作数是 TK_LOCAL_IDENT (e.g., %entry)
      Token bb_tok = *current_token(p);
      if (!expect(p, TK_LOCAL_IDENT))
      {
        parser_error(p, "Expected incoming basic block label (e.g., %entry) in PHI node");
        goto phi_error;
      }

      // [!!] 手动查找
      const char *label_name = bb_tok.as.ident_val;
      IRValueNode *bb_val = (IRValueNode *)ptr_hashmap_get(p->local_value_map, (void *)label_name);
      if (!bb_val || bb_val->kind != IR_KIND_BASIC_BLOCK)
      {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "Use of undefined basic block label '%%%s' in PHI node", label_name);
        parser_error(p, error_msg);
        goto phi_error;
      }

      IRBasicBlock *bb = container_of(bb_val, IRBasicBlock, label_address);
      ir_phi_add_incoming(phi_node, val, bb);

      if (!expect(p, TK_RBRACKET))
        goto phi_error;
      if (match(p, TK_COMMA) == false)
      {
        break; // 结束
      }
    }
    return phi_node;

  phi_error:
    parser_error(p, "Failed to parse PHI node incoming values");
    return NULL;
  }

  // 9. GEP (getelementptr)
  if (strcmp(opcode, "gep") == 0)
  {
    bool inbounds = false;

    // 1. 检查可选的 'inbounds' 关键字
    if (current_token(p)->type == TK_IDENT && strcmp(current_token(p)->as.ident_val, "inbounds") == 0)
    {
      inbounds = true;
      advance(p); // 消耗 'inbounds'
    }

    // 2. 解析源类型 (e.g., %MyStruct, [10 x i32])
    // 这是指针指向的类型, 而不是指针类型本身
    IRType *source_type = parse_type(p);
    if (!source_type)
    {
      parser_error(p, "Expected source type for 'gep'");
      return NULL;
    }

    if (!expect(p, TK_COMMA))
      return NULL;

    // 3. 解析基指针操作数 (e.g., ptr %a)
    IRType *ptr_type = parse_type(p);
    if (!ptr_type)
    {
      parser_error(p, "Expected pointer type for 'gep' base");
      return NULL;
    }
    // context.h API (ir_type_get_ptr) 保证了 'ptr_type'
    // 具有 IR_TYPE_PTR kind, 但这里需要验证它
    // 指向 source_type。

    // 3a. 验证它是否*是*一个指针
    if (ptr_type->kind != IR_TYPE_PTR)
    {
      char type_str_got[64];
      ir_type_to_string(ptr_type, type_str_got, sizeof(type_str_got));

      char error_msg[256];
      snprintf(error_msg, sizeof(error_msg), "Expected a pointer type for GEP base pointer, but got '%s'",
               type_str_got);
      parser_error(p, error_msg);
      return NULL;
    }

    // 3b. 验证它是否指向正确的 source_type
    if (ptr_type->as.pointee_type != source_type)
    {
      // 构造我们*期望*的类型 (ptr <source_type>)
      IRType *expected_ptr_type = ir_type_get_ptr(p->context, source_type);

      char type_str_expected[64];
      char type_str_got[64];
      ir_type_to_string(expected_ptr_type, type_str_expected, sizeof(type_str_expected));
      ir_type_to_string(ptr_type, type_str_got, sizeof(type_str_got));

      char error_msg[256];
      snprintf(error_msg, sizeof(error_msg),
               "GEP pointer type mismatch: expected pointer to source type (i.e., '%s'), but got '%s'",
               type_str_expected, type_str_got);
      parser_error(p, error_msg);
      return NULL;
    }

    // 3c. 解析操作数值
    IRValueNode *base_ptr = parse_operand(p, ptr_type);
    if (!base_ptr)
      return NULL;

    // 4. 解析索引列表 (e.g., , i64 0, i64 %i)
    bump_reset(&p->temp_arena);
    size_t capacity = 8;
    size_t count = 0;
    IRValueNode **indices = BUMP_ALLOC_SLICE(&p->temp_arena, IRValueNode *, capacity);
    if (!indices)
    {
      parser_error(p, "OOM in temp_arena for GEP indices");
      return NULL;
    }

    // 循环，只要我们看到一个 ','
    while (match(p, TK_COMMA))
    {
      // 检查扩容
      if (count == capacity)
      {
        size_t new_capacity = capacity * 2;
        IRValueNode **new_indices = BUMP_ALLOC_SLICE(&p->temp_arena, IRValueNode *, new_capacity);
        if (!new_indices)
        {
          parser_error(p, "OOM resizing GEP index list");
          return NULL;
        }
        memcpy(new_indices, indices, capacity * sizeof(IRValueNode *));
        indices = new_indices;
        capacity = new_capacity;
      }

      // 解析: <type> <value>
      IRType *idx_type = parse_type(p);
      if (!idx_type)
      {
        parser_error(p, "Expected type for GEP index");
        return NULL;
      }
      // (我们只接受整数索引)
      if (idx_type->kind < IR_TYPE_I1 || idx_type->kind > IR_TYPE_I64)
      {
        parser_error(p, "GEP indices must be integer types");
        return NULL;
      }

      IRValueNode *idx_val = parse_operand(p, idx_type);
      if (!idx_val)
      {
        parser_error(p, "Expected value for GEP index");
        return NULL;
      }

      indices[count++] = idx_val;
    }

    if (count == 0)
    {
      parser_error(p, "GEP instruction must have at least one index");
      return NULL;
    }

    // 5. 调用 Builder API
    return ir_builder_create_gep(p->builder, source_type, base_ptr, indices, count, inbounds);
  }

  // 10. Call
  if (strcmp(opcode, "call") == 0)
  {
    // 语法 (基于你的 IR 结构):
    // call @callee ( <arg1_type> <arg1_val>, <arg2_type> <arg2_val>, ... )

    // 1. 解析 Callee
    // [!!] 我们必须手动解析 callee, 因为它不是标准操作数
    Token callee_tok = *current_token(p);
    if (!expect(p, TK_GLOBAL_IDENT)) // (我们暂时只支持直接调用 @func)
    {
      parser_error(p, "Expected function name (e.g., @callee) after 'call'");
      return NULL;
    }
    IRValueNode *callee_val = parser_find_value(p, &callee_tok);
    if (!callee_val || callee_val->kind != IR_KIND_FUNCTION)
    {
      parser_error(p, "Call target must be a function");
      return NULL;
    }
    // [!!] 从 ValueNode* 获取 IRFunction*
    IRFunction *callee_func = container_of(callee_val, IRFunction, entry_address);

    // 2. 解析参数列表
    if (!expect(p, TK_LPAREN))
      return NULL;

    // 3. 重置 temp_arena 用于存储参数
    bump_reset(&p->temp_arena);
    size_t capacity = 8;
    size_t count = 0;
    IRValueNode **arg_values = BUMP_ALLOC_SLICE(&p->temp_arena, IRValueNode *, capacity);
    if (!arg_values)
    {
      parser_error(p, "OOM in temp_arena for call arguments");
      return NULL;
    }

    // 4. 遍历*预期*的参数 (来自函数定义)
    IDList *expected_arg_node = callee_func->arguments.next;

    // 检查非空参数列表: ')'
    if (current_token(p)->type != TK_RPAREN)
    {
      while (true)
      {
        // 4a. 检查参数数量 (太多)
        if (expected_arg_node == &callee_func->arguments)
        {
          parser_error(p, "Too many arguments provided to call");
          return NULL;
        }
        IRArgument *expected_arg = list_entry(expected_arg_node, IRArgument, list_node);
        IRType *expected_type = expected_arg->value.type;

        // 4b. 解析 <type>
        IRType *parsed_type = parse_type(p);
        if (!parsed_type)
          return NULL;

        // 4c. 验证类型匹配
        if (parsed_type != expected_type)
        {
          char type_str_expected[64];
          char type_str_got[64];
          ir_type_to_string(expected_type, type_str_expected, sizeof(type_str_expected));
          ir_type_to_string(parsed_type, type_str_got, sizeof(type_str_got));

          char error_msg[256];
          snprintf(error_msg, sizeof(error_msg),
                   "Argument type mismatch in call (arg %zu): expected '%s', but got '%s'",
                   count, // (count 是 0-indexed 的参数索引)
                   type_str_expected, type_str_got);
          parser_error(p, error_msg);
          return NULL;
        }

        // 4d. 解析 <value>
        IRValueNode *parsed_val = parse_operand(p, parsed_type);
        if (!parsed_val)
          return NULL;

        // 4e. 扩容并存储
        if (count == capacity)
        {
          size_t new_capacity = capacity * 2;
          IRValueNode **new_arg_values = BUMP_ALLOC_SLICE(&p->temp_arena, IRValueNode *, new_capacity);
          if (!new_arg_values)
          {
            parser_error(p, "OOM in temp_arena resizing call argument list");
            return NULL;
          }
          memcpy(new_arg_values, arg_values, capacity * sizeof(IRValueNode *));
          arg_values = new_arg_values;
          capacity = new_capacity;
        }
        arg_values[count++] = parsed_val;
        expected_arg_node = expected_arg_node->next;

        // 4f. 检查结束
        if (match(p, TK_RPAREN))
          break; // 结束
        if (!expect(p, TK_COMMA))
          return NULL;
      }
    }
    else
    {
      advance(p); // 消耗 ')' (空参数列表)
    }

    // 5. 检查参数数量 (太少)
    if (expected_arg_node != &callee_func->arguments)
    {
      parser_error(p, "Too few arguments provided to call");
      return NULL;
    }

    // 6. 调用 Builder API
    return ir_builder_create_call(p->builder, callee_val, arg_values, count);
  }

  // ... (其他指令, e.g., mul, sdiv) ...

  // 默认
  parser_error(p, "Unknown instruction opcode");
  return NULL;
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
 * | 'ptr' <type>  // (递归)
 * | '[' <count> 'x' <type> ']'
 * | '{' <type>, ... '}'
 * | '%' <name>  // (命名结构体)
 */
static IRType *
parse_type(Parser *p)
{
  Token *tok = current_token(p);
  IRType *parsed_type = NULL;

  switch (tok->type)
  {
  case TK_IDENT: {
    const char *name = tok->as.ident_val;
    // ... (处理 'void', 'i32', 'i64', 'f32', 'f64' ... 的代码) ...
    if (strcmp(name, "void") == 0)
    {
      parsed_type = ir_type_get_void(p->context);
    }
    else if (strcmp(name, "i1") == 0)
    {
      parsed_type = ir_type_get_i1(p->context);
    }
    else if (strcmp(name, "i8") == 0)
    {
      parsed_type = ir_type_get_i8(p->context);
    }
    else if (strcmp(name, "i16") == 0)
    {
      parsed_type = ir_type_get_i16(p->context);
    }
    else if (strcmp(name, "i32") == 0)
    {
      parsed_type = ir_type_get_i32(p->context);
    }
    else if (strcmp(name, "i64") == 0)
    {
      parsed_type = ir_type_get_i64(p->context);
    }
    else if (strcmp(name, "f32") == 0)
    {
      parsed_type = ir_type_get_f32(p->context);
    }
    else if (strcmp(name, "f64") == 0)
    {
      parsed_type = ir_type_get_f64(p->context);
    }
    // ... (处理 'ptr') ...
    else if (strcmp(name, "ptr") == 0)
    {
      advance(p);                           // 消耗 'ptr'
      IRType *pointee_type = parse_type(p); // 递归解析
      if (!pointee_type)
      {
        parser_error(p, "Expected pointee type after 'ptr'");
        return NULL;
      }
      return ir_type_get_ptr(p->context, pointee_type);
    }
    else
    {
      char error_msg[256];
      snprintf(error_msg, sizeof(error_msg), "Unknown type identifier '%s'", name);
      parser_error(p, error_msg);
      return NULL;
    }
    advance(p); // 消耗类型标识符 (e.g., 'i32')
    return parsed_type;
  }

  case TK_LBRACKET: // [
    advance(p);     // 消耗 '['
    return parse_array_type(p);

  case TK_LBRACE:                // {
    advance(p);                  // 消耗 '{'
    return parse_struct_type(p); // (用于 *匿名* 结构体)

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
    return found_type;
  }

  default:
    parser_error(p, "Expected a type signature");
    return NULL;
  }
}

/*
 * -----------------------------------------------------------------
 * --- 操作数与常量解析 (Operand & Constant Parsing) ---
 * -----------------------------------------------------------------
 */

/**
 * @brief 解析一个操作数 (值)
 *
 * 语法:
 * <constant_value>
 * | <local_ident>
 * | <global_ident>
 * | 'label' <local_ident>
 *
 * @param p Parser
 * @param type 操作数应有的类型 (由调用者解析)
 * @return IRValueNode*
 */
static IRValueNode *
parse_operand(Parser *p, IRType *type)
{
  // 复制 Token, 而不是获取指针。
  // 这样'tok' 就是栈上的一个安全副本。
  // 无论 'advance(p)' 被调用多少次, 'tok' 的内容都不会改变。
  Token tok = *current_token(p);

  switch (tok.type)
  {
  case TK_INTEGER_LITERAL: {
    // e.g., 5, -10
    int64_t val = tok.as.int_val;
    advance(p); // 消耗 literal

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
    default: {
      char type_str_expected[64];
      ir_type_to_string(type, type_str_expected, sizeof(type_str_expected));

      char error_msg[256];
      snprintf(error_msg, sizeof(error_msg), "Integer literal '%" PRId64 "' provided for non-integer type '%s'", val,
               type_str_expected);
      parser_error(p, error_msg);
      return NULL;
    }
    }
  }

  case TK_FLOAT_LITERAL: {
    double val = tok.as.float_val;
    advance(p); // 消耗 literal

    switch (type->kind)
    {
    case IR_TYPE_F32:
      return ir_constant_get_f32(p->context, (float)val);
    case IR_TYPE_F64:
      return ir_constant_get_f64(p->context, val);
    default: {
      char type_str_expected[64];
      ir_type_to_string(type, type_str_expected, sizeof(type_str_expected));

      char error_msg[256];
      snprintf(error_msg, sizeof(error_msg), "Float literal '%f' provided for non-float type '%s'", val,
               type_str_expected);
      parser_error(p, error_msg);
      return NULL;
    }
    }
  }

  case TK_IDENT: {
    // e.g., true, false, undef, null
    const char *name = tok.as.ident_val;

    if (strcmp(name, "true") == 0)
    {
      if (type->kind != IR_TYPE_I1)
      {
        parser_error(p, "Constant 'true' must have type 'i1'");
        return NULL;
      }
      advance(p); // 消耗 'true'
      return ir_constant_get_i1(p->context, true);
    }
    if (strcmp(name, "false") == 0)
    {
      if (type->kind != IR_TYPE_I1)
      {
        parser_error(p, "Constant 'false' must have type 'i1'");
        return NULL;
      }
      advance(p); // 消耗 'false'
      return ir_constant_get_i1(p->context, false);
    }
    if (strcmp(name, "undef") == 0)
    {
      advance(p); // 消耗 'undef'
      return ir_constant_get_undef(p->context, type);
    }
    // 'null' 是 'undef' 的指针类型别名
    if (strcmp(name, "null") == 0)
    {
      // [正确] (之前的假设是对的)
      if (type->kind != IR_TYPE_PTR)
      {
        parser_error(p, "Constant 'null' must have type 'ptr'");
        return NULL;
      }
      advance(p); // 消耗 'null'
      return ir_constant_get_undef(p->context, type);
    }

    // 如果是 'label' (e.g., in 'br label entry')
    if (strcmp(name, "label") == 0)
    {
      // [正确] (之前的假设是对的, type->kind == IR_TYPE_LABEL)
      if (type->kind != IR_TYPE_LABEL)
      {
        parser_error(p, "Operand 'label %...' is only valid where a label is expected (e.g., 'br')");
        return NULL;
      }

      advance(p); // 消耗 'label'
      // 'label' 后面必须跟一个 local_ident
      Token label_tok = *current_token(p);
      if (!expect(p, TK_LOCAL_IDENT))
      {
        parser_error(p, "Expected label name (e.g., %entry) after 'label'");
        return NULL;
      }
      // 查找该 'label' (它必须是一个 BasicBlock)
      // 我们手动查找, 不调用 parser_find_value
      // Lexer 保证 %entry 的 ident_val 是 "entry"
      const char *label_name = label_tok.as.ident_val;
      IRValueNode *val = (IRValueNode *)ptr_hashmap_get(p->local_value_map, (void *)label_name);

      if (!val) // [!!] 未找到? 创建前向声明
      {
        if (!p->current_function)
        {
          parser_error(p, "Cannot forward-declare a basic block outside a function");
          return NULL;
        }
        // 1. 创建块 (但不附加)
        IRBasicBlock *fwd_bb = ir_basic_block_create(p->current_function, label_name);
        if (!fwd_bb)
        {
          parser_error(p, "Failed to create forward declaration for basic block (OOM?)");
          return NULL;
        }
        // 2. 注册到符号表
        val = (IRValueNode *)&fwd_bb->label_address;
        ptr_hashmap_put(p->local_value_map, (void *)label_name, (void *)val);
        // (fwd_bb->list_node 仍然是 unlinked 状态)
      }

      // [!!] 验证它确实是一个基本块
      if (val->kind != IR_KIND_BASIC_BLOCK)
      {
        char error_msg[256];
        // (错误信息保持不变, 因为 %if_true 应该只代表标签)
        snprintf(error_msg, sizeof(error_msg), "Value '%%%s' used as label is not a basic block", label_name);
        parser_error(p, error_msg);
        return NULL;
      }
      return val;
    }

    parser_error(p, "Unexpected identifier as operand");
    return NULL;
  }

  case TK_LOCAL_IDENT: {
    // e.g., %1, %entry
    IRValueNode *val = parser_find_value(p, &tok);
    if (!val)
      return NULL; // find_value 已经报告了错误

    // 类型检查
    if (val->type != type)
    {
      char type_str_expected[64];
      char type_str_got[64];
      ir_type_to_string(type, type_str_expected, sizeof(type_str_expected));
      ir_type_to_string(val->type, type_str_got, sizeof(type_str_got));

      char error_msg[256];
      snprintf(error_msg, sizeof(error_msg), "Type mismatch for '%%%s': expected type '%s', but got '%s'",
               tok.as.ident_val, type_str_expected, type_str_got);
      parser_error(p, error_msg);
      return NULL;
    }

    advance(p); // 消耗 %ident
    return val;
  }

  case TK_GLOBAL_IDENT: {
    // e.g., @main, @gvar
    IRValueNode *val = parser_find_value(p, &tok);
    if (!val)
      return NULL; // find_value 已经报告了错误

    // 类型检查
    if (val->type != type)
    {
      char type_str_expected[64];
      char type_str_got[64];
      ir_type_to_string(type, type_str_expected, sizeof(type_str_expected));
      ir_type_to_string(val->type, type_str_got, sizeof(type_str_got));

      char error_msg[256];
      snprintf(error_msg, sizeof(error_msg), "Type mismatch for '@%s': expected type '%s', but got '%s'",
               tok.as.ident_val, type_str_expected, type_str_got);
      parser_error(p, error_msg);
      return NULL;
    }

    advance(p); // 消耗 @ident
    return val;
  }

  default:
    parser_error(p, "Expected an operand (constant, %local, or @global)");
    return NULL;
  }
}

/**
 * @brief 解析一个常量值 (用于全局变量初始化等)
 *
 * 语法: <type> <constant_value>
 * e.g., "i32 5", "i1 true", "ptr undef"
 *
 * @param p Parser
 * @return IRValueNode*
 */
static IRValueNode *
parse_constant(Parser *p)
{
  // 1. 解析类型
  IRType *type = parse_type(p);
  if (!type)
  {
    return NULL;
  }

  Token *tok = current_token(p);

  // 2. 解析值
  // 必须是字面量 (int, float) 或关键字 (true, false, undef, null)
  switch (tok->type)
  {
  case TK_INTEGER_LITERAL:
    // TK_FLOAT_LITERAL: (未来)
  case TK_IDENT: // (for true, false, undef, null)
    // 重用 parse_operand 来解析常量值
    return parse_operand(p, type);

  case TK_LOCAL_IDENT:
  case TK_GLOBAL_IDENT:
    parser_error(p, "Global variable initializer cannot reference another variable");
    return NULL;

  default:
    parser_error(p, "Expected a constant value after type");
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
  if (first_tok->type == TK_IDENT && strcmp(first_tok->as.ident_val, "source_filename") == 0)
  {
    ir_lexer_next(&lexer); // 消耗 'source_filename'

    // 检查 '='
    if (ir_lexer_current_token(&lexer)->type != TK_EQ)
    {
      fprintf(stderr, "Parse Error (Line %zu): Expected '=' after 'source_filename'\n",
              ir_lexer_current_token(&lexer)->line);
      ir_builder_destroy(builder);
      return NULL;
    }
    ir_lexer_next(&lexer); // 消耗 '='

    // 检查 "..." (字符串字面量)
    const Token *name_tok = ir_lexer_current_token(&lexer);
    if (name_tok->type != TK_STRING_LITERAL)
    {
      fprintf(stderr, "Parse Error (Line %zu): Expected string literal (e.g., \"foo.c\") after 'source_filename ='\n",
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
#include "ir/parser.h"

// 包含所有需要的 IR 和工具头文件
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
#include "utils/bump.h"
#include "utils/hashmap.h"
#include "utils/id_list.h" // for container_of

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// -----------------------------------------------------------------
// 辅助函数 (Helpers)
// -----------------------------------------------------------------

// --- 错误报告 ---
// (一个简单的版本，打印错误并返回 false)
static bool
parse_error(Parser *p, const char *message)
{
  fprintf(stderr, "Parse Error (line %d): %s. Near token type %d\n", p->lexer->current_token.line, message,
          p->lexer->current_token.type);
  // 让 Lexer 处于错误状态
  p->lexer->current_token.type = TK_ILLEGAL;
  return false;
}

// --- Token 消费 ---

// 获取当前 Token
static Token *
current_token(Parser *p)
{
  return &p->lexer->current_token;
}

// 消耗当前 Token 并前进到下一个
static void
consume_token(Parser *p)
{
  ir_lexer_next(p->lexer);
}

// 检查当前 Token 类型，如果匹配则消耗，否则报错
static bool
expect_token(Parser *p, TokenType expected, const char *error_message)
{
  if (current_token(p)->type != expected)
  {
    return parse_error(p, error_message);
  }
  consume_token(p);
  return true;
}

// 尝试消耗一个 Token，但不报错
static bool
maybe_consume_token(Parser *p, TokenType expected)
{
  if (current_token(p)->type == expected)
  {
    consume_token(p);
    return true;
  }
  return false;
}

// --- 符号表操作 ---

// 将 IRValueNode* 存入符号表
static void
add_value_to_scope(Parser *p, const char *name, IRValueNode *value)
{
  assert(name != NULL);
  assert(value != NULL);
  ptr_hashmap_put(p->value_map, (void *)name, value);
}

// 从符号表查找 IRValueNode*
static IRValueNode *
find_value_in_scope(Parser *p, const char *name)
{
  assert(name != NULL);
  return (IRValueNode *)ptr_hashmap_get(p->value_map, (void *)name);
}

// 将 IRBasicBlock* 存入块映射
static void
add_block_to_scope(Parser *p, const char *name, IRBasicBlock *bb)
{
  assert(name != NULL);
  assert(bb != NULL);
  ptr_hashmap_put(p->bb_map, (void *)name, bb);
}

// 从块映射查找 IRBasicBlock*
static IRBasicBlock *
find_block_in_scope(Parser *p, const char *name)
{
  assert(name != NULL);
  return (IRBasicBlock *)ptr_hashmap_get(p->bb_map, (void *)name);
}

// --- 前向声明解析函数 ---
static IRType *parse_type(Parser *p);
static IRValueNode *parse_value(Parser *p);
static bool parse_instruction(Parser *p);
static bool parse_basic_block(Parser *p);
static bool parse_function(Parser *p);
static bool parse_global_variable(Parser *p);
static bool parse_module_elements(Parser *p);

// --- 类型解析 (`parse_type`) ---
// 解析类型 (e.g., "i32", "ptr", "[10 x i32]", "%my_struct", "{ i32, ptr }")
static IRType *
parse_type(Parser *p)
{
  Token *tok = current_token(p);

  switch (tok->type)
  {
  case TK_IDENT: {
    const char *name = tok->as.ident_val;
    consume_token(p); // 消耗类型标识符

    if (strcmp(name, "void") == 0)
      return ir_type_get_void(p->context);
    if (strcmp(name, "i1") == 0)
      return ir_type_get_i1(p->context);
    if (strcmp(name, "i8") == 0)
      return ir_type_get_i8(p->context);
    if (strcmp(name, "i16") == 0)
      return ir_type_get_i16(p->context);
    if (strcmp(name, "i32") == 0)
      return ir_type_get_i32(p->context);
    if (strcmp(name, "i64") == 0)
      return ir_type_get_i64(p->context);
    if (strcmp(name, "f32") == 0)
      return ir_type_get_f32(p->context);
    if (strcmp(name, "f64") == 0)
      return ir_type_get_f64(p->context);
    if (strcmp(name, "label") == 0)
      return p->context->type_label; // [!!] 假设 Context 有
    if (strcmp(name, "ptr") == 0)
    {
      // 解析: ptr ( <type> ) -- 暂时只支持 'ptr'
      // 如果需要支持 ptr(ty), 需要取消注释 type.c 中的打印
      return ir_type_get_ptr(p->context, ir_type_get_void(p->context)); // [!!] 简化：默认为 void*
    }

    parse_error(p, "Unknown primitive type identifier");
    return NULL;
  }
  case TK_LBRACKET: { // 数组类型: [ <count> x <type> ]
    consume_token(p); // 吃掉 '['
    if (current_token(p)->type != TK_INTEGER_LITERAL)
    {
      parse_error(p, "Expected integer literal for array size");
      return NULL;
    }
    size_t count = (size_t)current_token(p)->as.int_val;
    consume_token(p); // 吃掉 size

    if (!expect_token(p, TK_IDENT, "Expected 'x' in array type") || strcmp(current_token(p)->as.ident_val, "x") != 0)
    {
      parse_error(p, "Expected 'x' in array type");
      return NULL; // [Fix] Check strcmp result
    }
    // consume_token(p); // 'x' 被 expect_token 吃了

    IRType *element_type = parse_type(p);
    if (!element_type)
      return NULL;

    if (!expect_token(p, TK_RBRACKET, "Expected ']' to close array type"))
      return NULL;

    return ir_type_get_array(p->context, element_type, count);
  }
  case TK_LBRACE: {   // 匿名结构体类型: { <type>, <type>, ... }
    consume_token(p); // 吃掉 '{'

    // [!!] 需要一个动态数组或 Arena 来存储成员类型
    Bump *temp_arena = &p->context->ir_arena; // 借用 IR Arena
    BumpMarker marker = bump_marker_get(temp_arena);

    IRType **member_types = NULL;
    size_t member_count = 0;
    size_t capacity = 0;

    while (current_token(p)->type != TK_RBRACE && current_token(p)->type != TK_EOF)
    {
      if (member_count > 0)
      {
        if (!expect_token(p, TK_COMMA, "Expected ',' between struct members"))
          goto struct_error;
      }

      IRType *member_type = parse_type(p);
      if (!member_type)
        goto struct_error;

      // 动态扩展数组 (简化版)
      if (member_count >= capacity)
      {
        capacity = (capacity == 0) ? 4 : capacity * 2;
        IRType **new_members = BUMP_ALLOC_SLICE(temp_arena, IRType *, capacity);
        if (!new_members)
        {
          parse_error(p, "Out of memory for struct members");
          goto struct_error;
        }
        if (member_types)
        {
          memcpy(new_members, member_types, member_count * sizeof(IRType *));
        }
        member_types = new_members;
      }
      member_types[member_count++] = member_type;
    }

    if (!expect_token(p, TK_RBRACE, "Expected '}' to close struct type"))
      goto struct_error;

    // 创建匿名结构体
    IRType *struct_type = ir_type_get_anonymous_struct(p->context, member_types, member_count);
    bump_marker_pop(temp_arena, marker); // 释放临时成员数组
    return struct_type;

  struct_error:
    bump_marker_pop(temp_arena, marker);
    return NULL;
  }
  case TK_LOCAL_IDENT: { // 命名结构体类型: %my_struct
    // (假设命名结构体在使用前已定义)
    const char *name = current_token(p)->as.ident_val;
    consume_token(p); // 吃掉 %name
    // [!!] 需要在 Context 中查找命名结构体
    // 假设有 ir_context_find_named_struct(ctx, name)
    IRType *found = NULL; // = ir_context_find_named_struct(p->context, name);
    if (!found)
    {
      // 在context.c中查找
      found = (IRType *)str_hashmap_get(p->context->named_struct_cache, name, strlen(name));
      if (!found)
      {
        parse_error(p, "Named struct type not found");
        return NULL;
      }
    }
    return found;
  }

  default:
    parse_error(p, "Unexpected token when parsing type");
    return NULL;
  }
}

// --- 值解析 (`parse_value`) ---
// 解析一个值 (e.g., "i32 10", "ptr @g", "i32 %x", "label %entry")
static IRValueNode *
parse_value(Parser *p)
{
  IRType *type = parse_type(p);
  if (!type)
    return NULL;

  Token *tok = current_token(p);

  switch (tok->type)
  {
  case TK_INTEGER_LITERAL: { // 常量整数: i32 10
    int64_t val = tok->as.int_val;
    consume_token(p);
    // [!!] 需要 Context API 来获取常量
    if (type == ir_type_get_i1(p->context))
      return ir_constant_get_i1(p->context, (bool)val);
    if (type == ir_type_get_i8(p->context))
      return ir_constant_get_i8(p->context, (int8_t)val);
    if (type == ir_type_get_i16(p->context))
      return ir_constant_get_i16(p->context, (int16_t)val);
    if (type == ir_type_get_i32(p->context))
      return ir_constant_get_i32(p->context, (int32_t)val);
    if (type == ir_type_get_i64(p->context))
      return ir_constant_get_i64(p->context, (int64_t)val);
    parse_error(p, "Integer literal type mismatch");
    return NULL;
  }
  case TK_IDENT: { // undef: i32 undef
    if (strcmp(tok->as.ident_val, "undef") == 0)
    {
      consume_token(p);
      return ir_constant_get_undef(p->context, type);
    }
    // [!!] 可能还需要处理浮点常量
    parse_error(p, "Expected constant value after type");
    return NULL;
  }
  case TK_LOCAL_IDENT: { // 局部变量: i32 %x
    const char *name = tok->as.ident_val;
    consume_token(p);
    IRValueNode *val = find_value_in_scope(p, name);
    if (!val)
    {
      parse_error(p, "Local value not found in scope");
      return NULL;
    }
    if (val->type != type)
    {
      parse_error(p, "Local value type mismatch");
      return NULL;
    }
    return val;
  }
  case TK_GLOBAL_IDENT: { // 全局变量/函数: ptr @g
    const char *name = tok->as.ident_val;
    consume_token(p);
    IRValueNode *val = find_value_in_scope(p, name);
    if (!val)
    {
      parse_error(p, "Global value not found in scope");
      return NULL;
    }
    if (val->type != type)
    {
      parse_error(p, "Global value type mismatch");
      return NULL;
    }
    return val;
  }
  case TK_LPAREN:   // 函数类型 TODO
  case TK_LBRACE:   // 结构体常量 TODO
  case TK_LBRACKET: // 数组常量 TODO
    parse_error(p, "Aggregate constants not implemented yet");
    return NULL;

  default:
    parse_error(p, "Unexpected token when parsing value");
    return NULL;
  }
}

// --- 指令解析 (`parse_instruction`) ---
// 解析单条指令 (e.g., "%res = add i32 %a, i32 %b")
static bool
parse_instruction(Parser *p)
{
  IRValueNode *result = NULL;
  const char *result_name = NULL;

  // 1. 检查是否有结果赋值 (e.g., "%res = ...")
  if (current_token(p)->type == TK_LOCAL_IDENT && p->lexer->ptr[0] == ' ' &&
      p->lexer->ptr[1] == '=') // [!!] 简单预读 '='
  {
    result_name = current_token(p)->as.ident_val;
    consume_token(p); // 吃掉 %name
    if (!expect_token(p, TK_EQ, "Expected '=' after result identifier"))
      return false;
  }

  // 2. 解析 Opcode
  if (current_token(p)->type != TK_IDENT)
  {
    return parse_error(p, "Expected instruction opcode identifier");
  }
  const char *opcode = current_token(p)->as.ident_val;
  consume_token(p); // 吃掉 opcode

  // 3. 根据 Opcode 分派
  IRValueNode *inst_val = NULL;

  // --- 终结者 ---
  if (strcmp(opcode, "ret") == 0)
  {
    if (current_token(p)->type == TK_IDENT && strcmp(current_token(p)->as.ident_val, "void") == 0)
    {
      consume_token(p); // 吃掉 void
      inst_val = ir_builder_create_ret(p->builder, NULL);
    }
    else
    {
      IRValueNode *ret_val = parse_value(p);
      if (!ret_val)
        return false;
      inst_val = ir_builder_create_ret(p->builder, ret_val);
    }
  }
  else if (strcmp(opcode, "br") == 0)
  {
    // 检查是 cond br 还是 uncond br
    IRValueNode *op1 = parse_value(p);
    if (!op1)
      return false;

    if (maybe_consume_token(p, TK_COMMA))
    {                                    // cond br: br i1 %cond, label %true, label %false
      IRValueNode *op2 = parse_value(p); // true label
      if (!op2)
        return false;
      if (!expect_token(p, TK_COMMA, "Expected ',' after true label"))
        return false;
      IRValueNode *op3 = parse_value(p); // false label
      if (!op3)
        return false;
      inst_val = ir_builder_create_cond_br(p->builder, op1, op2, op3);
    }
    else
    { // uncond br: br label %dest
      inst_val = ir_builder_create_br(p->builder, op1);
    }
  }
  // --- 内存操作 ---
  else if (strcmp(opcode, "alloc") == 0)
  { // [!!] 注意: dump 是 alloc, 这里要匹配
    IRType *alloc_type = parse_type(p);
    if (!alloc_type)
      return false;
    inst_val = ir_builder_create_alloca(p->builder, alloc_type);
  }
  else if (strcmp(opcode, "load") == 0)
  {
    IRType *res_type = parse_type(p);
    if (!res_type)
      return false;
    if (!expect_token(p, TK_COMMA, "Expected ',' after load type"))
      return false;
    IRValueNode *ptr_val = parse_value(p);
    if (!ptr_val)
      return false;
    inst_val = ir_builder_create_load(p->builder, res_type, ptr_val);
  }
  else if (strcmp(opcode, "store") == 0)
  {
    IRValueNode *val_to_store = parse_value(p);
    if (!val_to_store)
      return false;
    if (!expect_token(p, TK_COMMA, "Expected ',' after store value"))
      return false;
    IRValueNode *ptr_val = parse_value(p);
    if (!ptr_val)
      return false;
    inst_val = ir_builder_create_store(p->builder, val_to_store, ptr_val);
  }
  // --- 二元运算 ---
  else if (strcmp(opcode, "add") == 0 || strcmp(opcode, "sub") == 0)
  {
    IRValueNode *op1 = parse_value(p);
    if (!op1)
      return false;
    if (!expect_token(p, TK_COMMA, "Expected ',' after first operand"))
      return false;
    IRValueNode *op2 = parse_value(p);
    if (!op2)
      return false;
    if (op1->type != op2->type)
      return parse_error(p, "Binary operands have different types");

    if (strcmp(opcode, "add") == 0)
    {
      inst_val = ir_builder_create_add(p->builder, op1, op2);
    }
    else
    {
      inst_val = ir_builder_create_sub(p->builder, op1, op2);
    }
  }
  // --- ICMP ---
  else if (strcmp(opcode, "icmp") == 0)
  {
    if (current_token(p)->type != TK_IDENT)
      return parse_error(p, "Expected ICMP predicate");
    const char *pred_str = current_token(p)->as.ident_val;
    consume_token(p); // 吃掉 predicate

    IRICmpPredicate pred;
    if (strcmp(pred_str, "eq") == 0)
      pred = IR_ICMP_EQ;
    else if (strcmp(pred_str, "ne") == 0)
      pred = IR_ICMP_NE;
    // ... [!!] 添加所有其他谓词的映射 ...
    else
      return parse_error(p, "Unknown ICMP predicate");

    IRValueNode *op1 = parse_value(p);
    if (!op1)
      return false;
    if (!expect_token(p, TK_COMMA, "Expected ',' after first icmp operand"))
      return false;
    IRValueNode *op2 = parse_value(p);
    if (!op2)
      return false;
    if (op1->type != op2->type)
      return parse_error(p, "ICMP operands have different types");

    inst_val = ir_builder_create_icmp(p->builder, pred, op1, op2);
  }
  // --- PHI ---
  else if (strcmp(opcode, "phi") == 0)
  {
    IRType *phi_type = parse_type(p);
    if (!phi_type)
      return false;

    inst_val = ir_builder_create_phi(p->builder, phi_type);
    if (!inst_val)
      return false; // Builder 失败

    // 解析 [ val1, bb1 ], [ val2, bb2 ], ...
    while (current_token(p)->type != TK_SEMICOLON && // 假设注释或换行结束
           current_token(p)->type != TK_EOF)
    {
      if (maybe_consume_token(p, TK_COMMA))
      {
      } // 允许第一个没有逗号

      if (!expect_token(p, TK_LBRACKET, "Expected '[' for PHI incoming pair"))
        return false;

      IRValueNode *incoming_val = parse_value(p);
      if (!incoming_val)
        return false;
      if (!expect_token(p, TK_COMMA, "Expected ',' between PHI value and block"))
        return false;

      // [!!] 解析基本块标签 (不需要类型)
      if (current_token(p)->type != TK_LOCAL_IDENT)
        return parse_error(p, "Expected basic block label for PHI");
      const char *bb_name = current_token(p)->as.ident_val;
      consume_token(p); // 吃掉 %label

      IRBasicBlock *incoming_bb = find_block_in_scope(p, bb_name);
      if (!incoming_bb)
        return parse_error(p, "PHI incoming block label not found");

      if (!expect_token(p, TK_RBRACKET, "Expected ']' to close PHI incoming pair"))
        return false;

      // 添加到 PHI 节点
      ir_phi_add_incoming(inst_val, incoming_val, incoming_bb);

      // 如果下一个不是逗号，就结束
      if (current_token(p)->type != TK_COMMA)
        break;
    }
  }
  // --- GEP ---
  else if (strcmp(opcode, "gep") == 0)
  {
    bool inbounds = false;
    if (current_token(p)->type == TK_IDENT && strcmp(current_token(p)->as.ident_val, "inbounds") == 0)
    {
      inbounds = true;
      consume_token(p); // 吃掉 inbounds
    }

    IRType *source_type = parse_type(p);
    if (!source_type)
      return false;
    if (!expect_token(p, TK_COMMA, "Expected ',' after GEP source type"))
      return false;

    IRValueNode *base_ptr = parse_value(p);
    if (!base_ptr)
      return false;

    // [!!] 动态数组存索引
    Bump *temp_arena = &p->context->ir_arena;
    BumpMarker marker = bump_marker_get(temp_arena);
    IRValueNode **indices = NULL;
    size_t index_count = 0;
    size_t capacity = 0;

    while (maybe_consume_token(p, TK_COMMA))
    {
      IRValueNode *index = parse_value(p);
      if (!index)
        goto gep_error;

      if (index_count >= capacity)
      {
        capacity = (capacity == 0) ? 4 : capacity * 2;
        IRValueNode **new_indices = BUMP_ALLOC_SLICE(temp_arena, IRValueNode *, capacity);
        if (!new_indices)
        {
          parse_error(p, "Out of memory for GEP indices");
          goto gep_error;
        }
        if (indices)
        {
          memcpy(new_indices, indices, index_count * sizeof(IRValueNode *));
        }
        indices = new_indices;
      }
      indices[index_count++] = index;
    }

    inst_val = ir_builder_create_gep(p->builder, source_type, base_ptr, indices, index_count, inbounds);
    bump_marker_pop(temp_arena, marker); // 释放临时索引数组
    goto gep_finish;                     // 跳过错误处理

  gep_error:
    bump_marker_pop(temp_arena, marker);
    return false;
  gep_finish:; // 空语句
  }
  else
  {
    return parse_error(p, "Unknown instruction opcode");
  }

  // 4. 如果有结果，存入符号表
  if (result_name && inst_val)
  {
    add_value_to_scope(p, result_name, inst_val);
    // [!!] 设置 IRValueNode 的名字 (用于 dump)
    ir_value_set_name(inst_val, result_name);
  }
  else if (result_name && !inst_val)
  {
    return parse_error(p, "Instruction should produce a value but builder failed");
  }
  else if (!result_name && inst_val && inst_val->type->kind != IR_TYPE_VOID)
  {
    // [!!] 可能是内部错误或需要名字的指令 (如 alloca)
    // 暂时允许，但 dump 时可能需要名字
  }

  return true; // 指令解析成功
}

// --- 基本块解析 (`parse_basic_block`) ---
// 解析一个基本块 (label: instruction instruction ...)
static bool
parse_basic_block(Parser *p)
{
  // 1. 解析标签定义 (e.g., "entry:")
  if (current_token(p)->type != TK_LOCAL_IDENT)
  {
    return parse_error(p, "Expected basic block label identifier");
  }
  const char *bb_name = current_token(p)->as.ident_val;
  consume_token(p); // 吃掉 %label

  if (!expect_token(p, TK_COLON, "Expected ':' after basic block label"))
    return false;

  // 2. 查找或创建基本块
  IRBasicBlock *bb = find_block_in_scope(p, bb_name);
  if (!bb)
  {
    // 第一次遇到，创建它
    bb = ir_basic_block_create(p->current_function, bb_name);
    if (!bb)
      return parse_error(p, "Failed to create basic block");
    add_block_to_scope(p, bb_name, bb);
  }
  else
  {
    // 之前因为前向引用（如 br）已经创建过了，现在我们找到了它的定义
  }

  // 3. 设置 Builder 的插入点
  ir_builder_set_insertion_point(p->builder, bb);

  // 4. 解析块内的指令
  while (current_token(p)->type != TK_RBRACE &&                                        // 函数结束 '}'
         current_token(p)->type != TK_EOF && current_token(p)->type != TK_LOCAL_IDENT) // 下一个块标签开始
  {
    if (!parse_instruction(p))
    {
      return false; // 指令解析失败
    }
  }

  return true;
}

// --- 函数解析 (`parse_function`) ---
// 解析函数定义或声明
static bool
parse_function(Parser *p)
{
  // 1. 解析 'define' 或 'declare'
  bool is_declaration = false;
  if (current_token(p)->type != TK_IDENT)
    return parse_error(p, "Expected 'define' or 'declare'");
  if (strcmp(current_token(p)->as.ident_val, "declare") == 0)
  {
    is_declaration = true;
  }
  else if (strcmp(current_token(p)->as.ident_val, "define") != 0)
  {
    return parse_error(p, "Expected 'define' or 'declare'");
  }
  consume_token(p); // 吃掉 define/declare

  // 2. 解析返回类型
  IRType *ret_type = parse_type(p);
  if (!ret_type)
    return false;

  // 3. 解析函数名 (@name)
  if (current_token(p)->type != TK_GLOBAL_IDENT)
    return parse_error(p, "Expected function name (@...)");
  const char *func_name = current_token(p)->as.ident_val;
  consume_token(p); // 吃掉 @name

  // 4. 创建函数
  IRFunction *func = ir_function_create(p->module, func_name, ret_type);
  if (!func)
    return parse_error(p, "Failed to create function");
  p->current_function = func;                             // 设置当前函数
  add_value_to_scope(p, func_name, &func->entry_address); // 加入全局符号表

  // 5. 解析参数列表 '(' [type %name [, type %name]*] ')'
  if (!expect_token(p, TK_LPAREN, "Expected '(' after function name"))
    return false;

  while (current_token(p)->type != TK_RPAREN && current_token(p)->type != TK_EOF)
  {
    if (current_token(p)->type == TK_RPAREN)
      break; // 可能是空参数列表

    if (ptr_hashmap_size(p->value_map) > 1)
    { // 如果不是第一个参数
      if (!expect_token(p, TK_COMMA, "Expected ',' between function arguments"))
        return false;
    }

    IRType *arg_type = parse_type(p);
    if (!arg_type)
      return false;

    const char *arg_name = NULL;
    if (current_token(p)->type == TK_LOCAL_IDENT)
    {
      arg_name = current_token(p)->as.ident_val;
      consume_token(p); // 吃掉 %name
    }
    else
    {
      // 允许没有名字的参数
    }

    // 创建参数并加入符号表
    IRArgument *arg = ir_argument_create(func, arg_type, arg_name);
    if (!arg)
      return parse_error(p, "Failed to create argument");
    if (arg_name)
    {
      add_value_to_scope(p, arg_name, &arg->value);
    }
  }

  if (!expect_token(p, TK_RPAREN, "Expected ')' after function arguments"))
    return false;

  // 6. 解析函数体 '{' block* '}' (如果不是声明)
  if (!is_declaration)
  {
    if (!expect_token(p, TK_LBRACE, "Expected '{' to start function body"))
      return false;

    // [!!] 在解析块之前，清空局部符号表 (value_map)
    // 但保留全局符号 (函数名, 全局变量)
    // (简单的做法：创建一个新的 map，完成后恢复旧 map)
    PtrHashMap *old_value_map = p->value_map;
    p->value_map = ptr_hashmap_create(&p->context->ir_arena, 64); // 新的局部作用域
    // 复制全局符号到新 map
    // TODO: 需要一个迭代 PtrHashMap 的方法

    // [!!] 将参数加入新的局部作用域
    IDList *arg_iter;
    list_for_each(&func->arguments, arg_iter)
    {
      IRArgument *arg = list_entry(arg_iter, IRArgument, list_node);
      if (arg->value.name)
      { // 只添加有名字的
        add_value_to_scope(p, arg->value.name, &arg->value);
      }
    }

    // 预扫描所有基本块标签 (处理前向引用)
    // TODO: 这需要 Lexer 支持状态保存/恢复，或者多次扫描

    // 解析所有基本块
    while (current_token(p)->type != TK_RBRACE && current_token(p)->type != TK_EOF)
    {
      if (!parse_basic_block(p))
      {
        p->value_map = old_value_map; // 恢复旧 map
        return false;
      }
    }

    if (!expect_token(p, TK_RBRACE, "Expected '}' to end function body"))
    {
      p->value_map = old_value_map; // 恢复旧 map
      return false;
    }

    p->value_map = old_value_map; // 恢复旧 map
  }

  p->current_function = NULL; // 退出函数作用域
  return true;
}

// --- 全局变量解析 (`parse_global_variable`) ---
// 解析全局变量定义 (e.g., "@g = global i32 10")
static bool
parse_global_variable(Parser *p)
{
  // 1. 解析全局变量名 (@name)
  if (current_token(p)->type != TK_GLOBAL_IDENT)
    return parse_error(p, "Expected global variable name (@...)");
  const char *global_name = current_token(p)->as.ident_val;
  consume_token(p); // 吃掉 @name

  if (!expect_token(p, TK_EQ, "Expected '=' after global variable name"))
    return false;
  if (current_token(p)->type != TK_IDENT || strcmp(current_token(p)->as.ident_val, "global") != 0)
  {
    return parse_error(p, "Expected 'global' keyword");
  }
  consume_token(p); // 吃掉 'global'

  // 2. 解析类型
  IRType *alloc_type = parse_type(p);
  if (!alloc_type)
    return false;

  // 3. 解析初始值 (必须是常量)
  IRValueNode *initializer = NULL;
  if (current_token(p)->type != TK_SEMICOLON && // 假设换行或注释结束
      current_token(p)->type != TK_EOF)
  {
    // 尝试解析一个常量值
    IRValueNode *init_val = parse_value(p);
    if (!init_val)
      return false;
    if (init_val->kind != IR_KIND_CONSTANT)
      return parse_error(p, "Global variable initializer must be a constant");
    if (init_val->type != alloc_type)
      return parse_error(p, "Global variable initializer type mismatch");
    initializer = init_val;
  }

  // 4. 创建全局变量
  IRGlobalVariable *global = ir_global_variable_create(p->module, global_name, alloc_type, initializer);
  if (!global)
    return parse_error(p, "Failed to create global variable");

  // 5. 加入全局符号表
  add_value_to_scope(p, global_name, &global->value);

  return true;
}

// --- 模块元素解析 (`parse_module_elements`) ---
// 解析顶层元素 (全局变量, 函数定义/声明)
static bool
parse_module_elements(Parser *p)
{
  while (current_token(p)->type != TK_EOF)
  {
    // 尝试解析全局变量 (@...)
    if (current_token(p)->type == TK_GLOBAL_IDENT)
    {
      // 预读 '=' 以区分函数调用和全局定义
      // [!!] 这是一个简化的预读
      const char *peek_ptr = p->lexer->ptr;
      while (*peek_ptr == ' ' || *peek_ptr == '\t')
        peek_ptr++;
      if (*peek_ptr == '=')
      {
        if (!parse_global_variable(p))
          return false;
        continue; // 继续下一个顶层元素
      }
      // 如果不是 '=', 那它应该是一个函数调用或函数地址，
      // 但在顶层不应该出现。我们假设它是一个函数定义/声明的开始
    }

    // 尝试解析函数 ('define' 或 'declare')
    if (current_token(p)->type == TK_IDENT && (strcmp(current_token(p)->as.ident_val, "define") == 0 ||
                                               strcmp(current_token(p)->as.ident_val, "declare") == 0))
    {
      if (!parse_function(p))
        return false;
    }
    // 解析命名结构体定义 (e.g., "%my_struct = type { i32 }")
    else if (current_token(p)->type == TK_LOCAL_IDENT)
    {
      const char *struct_name = current_token(p)->as.ident_val;
      Token next_tok = p->lexer->peek_token; // [!!] 假设 Lexer 有 peek
      // 非常简化的检查: %name = type { ... }
      // (需要 Lexer 支持 peek!)
      // if (next_tok.type == TK_EQ) { ... }
      // TODO: 实现命名结构体解析
      return parse_error(p, "Named struct definition parsing not implemented");
    }
    else
    {
      return parse_error(p, "Unexpected token at top level");
    }
  }
  return true;
}

// -----------------------------------------------------------------
// 公共 API (Public API)
// -----------------------------------------------------------------

Parser *
ir_parser_create(IRContext *ctx, IRModule *mod)
{
  assert(ctx && mod);
  Parser *p = (Parser *)malloc(sizeof(Parser));
  if (!p)
    return NULL;

  p->context = ctx;
  p->module = mod;
  p->lexer = (Lexer *)malloc(sizeof(Lexer)); // Lexer 需要单独分配
  if (!p->lexer)
  {
    free(p);
    return NULL;
  }

  // Builder 在 parse_function 中创建/销毁可能更合适
  p->builder = ir_builder_create(ctx);
  if (!p->builder)
  {
    free(p->lexer);
    free(p);
    return NULL;
  }

  p->current_function = NULL;

  // [!!] Arena 用于 HashMap 分配
  Bump *arena = &ctx->ir_arena; // 假设 HashMap 用 IR Arena
  p->value_map = ptr_hashmap_create(arena, 64);
  p->bb_map = ptr_hashmap_create(arena, 32);
  if (!p->value_map || !p->bb_map)
  {
    ir_builder_destroy(p->builder);
    free(p->lexer);
    free(p);
    return NULL;
  }

  return p;
}

void
ir_parser_destroy(Parser *parser)
{
  if (!parser)
    return;
  ir_builder_destroy(parser->builder);
  free(parser->lexer);
  // HashMaps 在 Arena 上，无需 free
  free(parser);
}

bool
ir_parse_buffer(Parser *parser, const char *buffer)
{
  assert(parser && buffer);

  // 1. 初始化 Lexer
  ir_lexer_init(parser->lexer, buffer, parser->context);

  // 2. 清空符号表 (以防重用 Parser)
  // ptr_hashmap_clear(parser->value_map); // [!!] 假设有 clear API
  // ptr_hashmap_clear(parser->bb_map);

  // 3. 开始解析顶层元素
  if (!parse_module_elements(parser))
  {
    return false; // 解析失败
  }

  // 4. 检查是否到达 EOF
  if (current_token(parser)->type != TK_EOF)
  {
    return parse_error(parser, "Expected end of file after parsing module elements");
  }

  return true; // 解析成功
}
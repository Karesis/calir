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




#include "ir/value.h"
#include "ir/constant.h"
#include "ir/function.h"
#include "ir/global.h"
#include "ir/printer.h"
#include "ir/type.h"
#include "ir/use.h"
#include "utils/id_list.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief [内部] 从一个 ValueNode 向上查找到 IRContext
 *
 * 这依赖于 container_of 宏 (来自 id_list.h) 和 parent 指针。
 */
static IRContext *
get_context_from_value(IRValueNode *val)
{

  switch (val->kind)
  {
  case IR_KIND_INSTRUCTION: {
    IRInstruction *inst = container_of(val, IRInstruction, result);
    if (!inst->parent)
      return NULL;
    if (!inst->parent->parent)
      return NULL;
    if (!inst->parent->parent->parent)
      return NULL;
    return inst->parent->parent->parent->context;
  }
  case IR_KIND_BASIC_BLOCK: {
    IRBasicBlock *bb = container_of(val, IRBasicBlock, label_address);
    if (!bb->parent)
      return NULL;
    if (!bb->parent->parent)
      return NULL;
    return bb->parent->parent->context;
  }
  case IR_KIND_ARGUMENT: {
    IRArgument *arg = container_of(val, IRArgument, value);
    if (!arg->parent)
      return NULL;
    if (!arg->parent->parent)
      return NULL;
    return arg->parent->parent->context;
  }
  case IR_KIND_FUNCTION: {
    IRFunction *func = container_of(val, IRFunction, entry_address);
    if (!func->parent)
      return NULL;
    return func->parent->context;
  }
  case IR_KIND_GLOBAL: {

    IRGlobalVariable *gvar = container_of(val, IRGlobalVariable, value);
    if (!gvar->parent)
      return NULL;
    return gvar->parent->context;
  }

  case IR_KIND_CONSTANT:
  default:
    return NULL;
  }
}

/**
 * @brief [内部] 辅助函数, 仅打印常量的值
 */
static void
ir_constant_dump_value(IRConstant *konst, IRPrinter *p)
{
  if (konst->const_kind == CONST_KIND_INT)
  {
    ir_printf(p, "%d", konst->data.int_val);
  }
  else if (konst->const_kind == CONST_KIND_UNDEF)
  {
    ir_print_str(p, "undef");
  }

}

/**
 * @brief [新] 打印一个 Value 的 "名字" (e.g., "%a", "@main", "$entry", "10")
 */
void
ir_value_dump_name(IRValueNode *val, IRPrinter *p)
{
  if (!val)
  {
    ir_print_str(p, "<null_val>");
    return;
  }


  if (val->kind == IR_KIND_CONSTANT)
  {
    ir_constant_dump_value((IRConstant *)val, p);
    return;
  }


  assert(val->name != NULL && "Value name is NULL");

  switch (val->kind)
  {
  case IR_KIND_BASIC_BLOCK:
    ir_printf(p, "$%s", val->name);
    break;
  case IR_KIND_FUNCTION:
  case IR_KIND_GLOBAL:
    ir_printf(p, "@%s", val->name);
    break;
  case IR_KIND_ARGUMENT:
  case IR_KIND_INSTRUCTION:
    ir_printf(p, "%%%s", val->name);
    break;
  default:
    ir_printf(p, "<??_KIND_%d>", val->kind);
    break;
  }
}

/**
 * @brief [新] 打印一个 Value 作为 "操作数" (e.g., "%a: i32", "10: i32", "$entry")
 */
void
ir_value_dump_with_type(IRValueNode *val, IRPrinter *p)
{
  if (!val)
  {
    ir_print_str(p, "<null_operand>");
    return;
  }


  ir_value_dump_name(val, p);


  switch (val->kind)
  {
  case IR_KIND_CONSTANT:
  case IR_KIND_ARGUMENT:
  case IR_KIND_INSTRUCTION:

    ir_print_str(p, ": ");


    ir_type_dump(val->type, p);
    break;

  case IR_KIND_BASIC_BLOCK:
  case IR_KIND_FUNCTION:
  case IR_KIND_GLOBAL:

    break;

  default:
    break;
  }
}

/**
 * @brief [已重构] 旧的 dump 函数, 现在只是 _with_type 的别名
 *
 * 为了兼容性, 我们保留 ir_value_dump,
 * 并让它调用新的规范化函数。
 */
void
ir_value_dump(IRValueNode *val, IRPrinter *p)
{

  ir_value_dump_with_type(val, p);
}

/**
 * @brief 安全地设置 Value 的名字 (自动 free 旧名字并 strdup 新名字)
 */
void
ir_value_set_name(IRValueNode *val, const char *name)
{
  assert(val != NULL);


  if (name)
  {

    IRContext *ctx = get_context_from_value(val);
    assert(ctx != NULL && "Could not find IRContext from IRValueNode!");




    val->name = (char *)ir_context_intern_str(ctx, name);
  }
  else
  {
    val->name = NULL;
  }
}

/**
 * @brief (核心优化函数) 替换所有对 old_val 的使用为 new_val
 */
void
ir_value_replace_all_uses_with(IRValueNode *old_val, IRValueNode *new_val)
{
  assert(old_val != NULL);
  assert(new_val != NULL);


  if (old_val == new_val)
  {
    return;
  }



  IDList *iter, *temp;
  list_for_each_safe(&old_val->uses, iter, temp)
  {


    IRUse *use = list_entry(iter, IRUse, value_node);





    ir_use_set_value(use, new_val);
  }


  assert(list_empty(&old_val->uses));
}
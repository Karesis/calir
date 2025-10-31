// src/ir/value.c

#include "ir/value.h"
#include "ir/constant.h"
#include "ir/function.h"
#include "ir/global.h"
#include "ir/type.h" // <-- 关键依赖, 用于 dump
#include "ir/use.h"  // <-- 关键依赖, 用于 R-A-U-W
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
    // 全局变量 (IRGlobalVariable) 也有一个 parent (IRModule)
    IRGlobalVariable *gvar = container_of(val, IRGlobalVariable, value);
    if (!gvar->parent)
      return NULL;
    return gvar->parent->context;
  }
  // 常量没有parent指针
  case IR_KIND_CONSTANT:
  default:
    return NULL; // 无法安全找到
  }
}

/**
 * @brief [内部] 辅助函数, 仅打印常量的值
 */
static void
ir_constant_dump_value(IRConstant *konst, FILE *stream)
{
  if (konst->const_kind == CONST_KIND_INT)
  {
    fprintf(stream, "%d", konst->data.int_val); // [TODO] 未来可能需要处理 i64
  }
  else if (konst->const_kind == CONST_KIND_UNDEF)
  {
    fprintf(stream, "undef");
  }
  // [TODO] 未来添加 CONST_KIND_FLOAT 等
}

/**
 * @brief [新] 打印一个 Value 的 "名字" (e.g., "%a", "@main", "$entry", "10")
 */
void
ir_value_dump_name(IRValueNode *val, FILE *stream)
{
  if (!val)
  {
    fprintf(stream, "<null_val>");
    return;
  }

  // 常量没有 'name' 字段, 它打印自己的值
  if (val->kind == IR_KIND_CONSTANT)
  {
    ir_constant_dump_value((IRConstant *)val, stream);
    return;
  }

  // 所有其他类型都应该有 'name'
  assert(val->name != NULL && "Value name is NULL");

  switch (val->kind)
  {
  case IR_KIND_BASIC_BLOCK:
    fprintf(stream, "$%s", val->name); // 新规范: $label
    break;
  case IR_KIND_FUNCTION:
  case IR_KIND_GLOBAL:
    fprintf(stream, "@%s", val->name); // 新规范: @name
    break;
  case IR_KIND_ARGUMENT:
  case IR_KIND_INSTRUCTION:
    fprintf(stream, "%%%s", val->name); // 新规范: %name
    break;
  default:
    fprintf(stream, "<??_KIND_%d>", val->kind);
    break;
  }
}

/**
 * @brief [新] 打印一个 Value 作为 "操作数" (e.g., "%a: i32", "10: i32", "$entry")
 */
void
ir_value_dump_with_type(IRValueNode *val, FILE *stream)
{
  if (!val)
  {
    fprintf(stream, "<null_operand>");
    return;
  }

  // 1. 打印 "名字" (e.g., "%a", "10", "$entry", "@main")
  ir_value_dump_name(val, stream);

  // 2. 打印 ": type", 但 $label 和 @func/@global 在使用时不需要
  switch (val->kind)
  {
  case IR_KIND_CONSTANT:
  case IR_KIND_ARGUMENT:
  case IR_KIND_INSTRUCTION:
    // 打印: ": i32" 或 ": <i32>"
    fprintf(stream, ": ");
    ir_type_dump(val->type, stream); // (这个函数我们已经重构好了)
    break;

  case IR_KIND_BASIC_BLOCK:
  case IR_KIND_FUNCTION:
  case IR_KIND_GLOBAL:
    // 这三种类型在使用时 (e.g. br $entry, call @main) 不打印类型
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
ir_value_dump(IRValueNode *val, FILE *stream)
{
  // 委托给新的、更强大的打印函数
  ir_value_dump_with_type(val, stream);
}

/**
 * @brief 安全地设置 Value 的名字 (自动 free 旧名字并 strdup 新名字)
 */
void
ir_value_set_name(IRValueNode *val, const char *name)
{
  assert(val != NULL);

  // 复制新名字
  if (name)
  {
    // 找到 Context
    IRContext *ctx = get_context_from_value(val);
    assert(ctx != NULL && "Could not find IRContext from IRValueNode!");

    // 使用字符串驻留 (String Interning)
    // (需要一个 (char*) 转换，因为 val->name 是 char*,
    //  而 intern_str 返回 const char*)
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

  // 不能用自己替换自己
  if (old_val == new_val)
  {
    return;
  }

  // 遍历 old_val 的 'uses' 链表
  // 必须使用 'safe' 版本, 因为 ir_use_set_value 会修改这个链表
  IDList *iter, *temp;
  list_for_each_safe(&old_val->uses, iter, temp)
  {
    // iter 是 IDList* (即 use->value_node)
    // 我们用 list_entry 从成员 'value_node' 找到 'IRUse' 容器
    IRUse *use = list_entry(iter, IRUse, value_node);

    // ir_use_set_value 会自动处理:
    // 1. 从 old_val->uses 移除 (list_del(&use->value_node))
    // 2. 更新 use->value = new_val
    // 3. 添加到 new_val->uses (list_add_tail(&new_val->uses, &use->value_node))
    ir_use_set_value(use, new_val);
  }

  // 此时, old_val->uses 链表应该被清空了
  assert(list_empty(&old_val->uses));
}
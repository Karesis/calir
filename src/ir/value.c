// src/ir/value.c

#include "ir/value.h"
#include "ir/constant.h"
#include "ir/type.h" // <-- 关键依赖, 用于 dump
#include "ir/use.h"  // <-- 关键依赖, 用于 R-A-U-W

#include <assert.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief 打印一个 Value 的引用 (e.g., "i32 %foo" or "label %entry")
 */
void
ir_value_dump(IRValueNode *val, FILE *stream)
{
  if (!val)
  {
    fprintf(stream, "<null_val>");
    return;
  }

  // --- 新增：处理常量 ---
  if (val->kind == IR_KIND_CONSTANT)
  {
    IRConstant *konst = (IRConstant *)val; // 向下转型

    // 1. 打印类型
    ir_type_dump(konst->value.type, stream);
    fprintf(stream, " ");

    // 2. 打印常量的值
    if (konst->const_kind == CONST_KIND_INT)
    {
      fprintf(stream, "%d", konst->data.int_val);
    }
    else if (konst->const_kind == CONST_KIND_UNDEF)
    {
      fprintf(stream, "undef");
    }
    return; // *** 提前返回 ***
  }
  // --- 修改结束 ---

  // 基本块标签
  if (val->kind == IR_KIND_BASIC_BLOCK)
  {
    // 标签没有类型, 只有名字
    fprintf(stream, "label %%%s", val->name);
  }
  // 其他 (参数, 指令结果, 函数, ...)
  else
  {
    // 打印类型
    ir_type_dump(val->type, stream);

    // 打印名字
    if (val->kind == IR_KIND_FUNCTION)
    {
      fprintf(stream, " @%s", val->name); // 函数用 '@'
    }
    else
    {
      fprintf(stream, " %%%s", val->name); // 局部值用 '%'
    }
  }
}

/**
 * @brief 安全地设置 Value 的名字 (自动 free 旧名字并 strdup 新名字)
 */
void
ir_value_set_name(IRValueNode *val, const char *name)
{
  assert(val != NULL);

  // 释放旧名字 (如果有)
  if (val->name)
  {
    free(val->name);
  }

  // 复制新名字
  if (name)
  {
    val->name = strdup(name);
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
    // iter 是 IRUse->def_node
    IRUse *use = list_entry(iter, IRUse, def_node);

    // ir_use_set_value 会自动处理:
    // 1. 从 old_val->uses 移除
    // 2. 更新 use->value = new_val
    // 3. 添加到 new_val->uses
    ir_use_set_value(use, new_val);
  }

  // 此时, old_val->uses 链表应该被清空了
  assert(list_empty(&old_val->uses));
}
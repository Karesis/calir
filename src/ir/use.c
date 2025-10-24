#include "ir/use.h"
#include "ir/instruction.h"
#include "ir/value.h"
#include <assert.h> // for assert
#include <stdlib.h> // for malloc, free

/**
 * @brief 创建并初始化一个 Use 边 (但尚未链接)
 *
 * @param user 拥有这个 Use 的指令 (User)
 * @param val 这个 Use 正在使用的值 (Value)
 * @return 一个新的 (malloc'd) IRUse 对象
 */
IRUse *
ir_use_create(IRInstruction *user, IRValueNode *val)
{
  assert(user != NULL && "User (Instruction) cannot be NULL");
  assert(val != NULL && "Value (ValueNode) cannot be NULL");

  IRUse *use = (IRUse *)malloc(sizeof(IRUse));
  if (!use)
  {
    // 处理内存分配失败
    return NULL;
  }

  use->user = user;
  use->value = val;

  // 初始化两个链表节点，将它们置于 "unlinked" 状态
  // (prev 和 next 都指向自己)
  list_init(&use->user_node);
  list_init(&use->def_node);

  return use;
}

/**
 * @brief 销毁一个 Use 边
 * (注意：销毁前必须先调用 ir_use_unlink，否则会导致链表损坏)
 */
void
ir_use_destroy(IRUse *use)
{
  if (!use)
  {
    return;
  }

  // 断言，确保这个 use 节点已经被 unlinked
  assert(list_empty(&use->user_node)); // list_empty 检查 node->next == node
  assert(list_empty(&use->def_node));

  free(use);
}

/**
 * @brief 将一个 Use 边链接到 User 和 Value
 *
 * 1. 将 use->user_node 添加到 user->operands 链表
 * 2. 将 use->def_node 添加到 value->uses 链表
 */
void
ir_use_link(IRUse *use)
{
  assert(use != NULL);

  // 1. 添加到 User (Instruction) 的 operands 链表尾部
  list_add_tail(&use->user->operands, &use->user_node);

  // 2. 添加到 Value (ValueNode) 的 uses 链表尾部
  list_add_tail(&use->value->uses, &use->def_node);
}

/**
 * @brief 解除一个 Use 边的链接 (从两个链表中移除)
 *
 * 1. 从 user->operands 链表移除
 * 2. 从 value->uses 链表移除
 */
void
ir_use_unlink(IRUse *use)
{
  assert(use != NULL);

  // 从 "User" (Instruction) 的 operands 链表中移除
  list_del(&use->user_node);

  // 从 "Value" (ValueNode) 的 uses 链表中移除
  list_del(&use->def_node);
}

/**
 * @brief 替换一个 Use 边当前指向的 Value
 * (例如, 在指令优化时, "add %a, %b" 变为 "add %a, %c")
 *
 * 这会自动处理旧 Value 和新 Value 的 'uses' 链表更新。
 *
 * @param use 要修改的 Use
 * @param new_val 新的 Value
 */
void
ir_use_set_value(IRUse *use, IRValueNode *new_val)
{
  assert(use != NULL);
  assert(new_val != NULL && "New value cannot be NULL");

  // 如果值没有变化，直接返回
  if (use->value == new_val)
  {
    return;
  }

  // 1. 从旧 Value 的 uses 链表中移除
  list_del(&use->def_node);

  // 2. 更新 use 指向的 Value
  use->value = new_val;

  // 3. 添加到新 Value 的 uses 链表中
  list_add_tail(&new_val->uses, &use->def_node);
}
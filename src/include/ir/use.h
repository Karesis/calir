#ifndef IR_USE_H
#define IR_USE_H

#include "id_list.h"
#include "ir/instruction.h"
#include "ir/value.h"

// --- IRUse 结构 ---

/**
 * @brief "Use" 边 (Def-Use / Use-Def 链的核心)
 *
 * 这个对象代表一个 "User" (如 IRInstruction)
 * 对一个 "Value" (IRValueNode) 的使用。
 *
 * 它同时被链接在两个不同的链表中:
 * 1. 它被加入到 "User" 的 "operands" 链表中 (通过 user_node)。
 * (即 IRInstruction->operands 链表)
 *
 * 2. 它被加入到 "Value" 的 "uses" 链表中 (通过 def_node)。
 * (即 IRValueNode->uses 链表)
 */
typedef struct
{
  /** Use-Def 链: 指向此 Use 正在使用的 "Value" (Definition) */
  IRValueNode *value;

  /** Def-Use 链: 指向拥有此 Use 的 "User" (Instruction) */
  IRInstruction *user;

  /** * 节点，用于加入 User->operands 链表
   * (e.g., IRInstruction->operands)
   *
   * 通过 list_entry(node, IRUse, user_node) 获取 IRUse
   */
  IDList user_node;

  /**
   * 节点，用于加入 Value->uses 链表
   * (e.g., IRValueNode->uses)
   *
   * 通过 list_entry(node, IRUse, def_node) 获取 IRUse
   */
  IDList def_node;

} IRUse;

// --- API 函数 ---

/**
 * @brief 创建并初始化一个 Use 边 (但尚未链接)
 *
 * @param user 拥有这个 Use 的指令 (User)
 * @param val 这个 Use 正在使用的值 (Value)
 * @return 一个新的 (malloc'd) IRUse 对象
 */
IRUse *ir_use_create(IRInstruction *user, IRValueNode *val);

/**
 * @brief 销毁一个 Use 边
 * (注意：销毁前必须先调用 ir_use_unlink)
 */
void ir_use_destroy(IRUse *use);

/**
 * @brief 将一个 Use 边链接到 User 和 Value
 *
 * 1. 将 use->user_node 添加到 user->operands 链表
 * 2. 将 use->def_node 添加到 value->uses 链表
 */
void ir_use_link(IRUse *use);

/**
 * @brief 解除一个 Use 边的链接 (从两个链表中移除)
 *
 * 1. 从 user->operands 链表移除
 * 2. 从 value->uses 链表移除
 */
void ir_use_unlink(IRUse *use);

/**
 * @brief 替换一个 Use 边当前指向的 Value
 * (例如, 在指令优化时, "add %a, %b" 变为 "add %a, %c")
 *
 * 这会自动处理旧 Value 和新 Value 的 'uses' 链表更新。
 *
 * @param use 要修改的 Use
 * @param new_val 新的 Value
 */
void ir_use_set_value(IRUse *use, IRValueNode *new_val);

#endif
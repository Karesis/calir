#ifndef FUNCTION_H
#define FUNCTION_H

#include "ir/module.h"
#include "ir/type.h"
#include "ir/value.h"
#include "utils/id_list.h"
#include <stdio.h>

/**
 * @brief 函数
 */
typedef struct IRFunction
{
  IRValueNode entry_address; // 函数入口地址
  IDList list_node;          // <-- 节点，用于加入 Module->functions 链表

  IRType *return_type;
  IDList arguments;    // 链表头 (元素是 IRArgument)
  IDList basic_blocks; // 链表头 (元素是 IRBasicBlock)
  IRModule *parent;    // <-- 指向父模块
} IRFunction;

/**
 * @brief 函数参数
 */
typedef struct IRArgument
{
  IRValueNode value;
  IDList list_node;   // <-- 节点，用于加入 Function->arguments 链表
  IRFunction *parent; // <-- 指向父函数
} IRArgument;

/**
 * @brief 创建一个新函数 (在 Arena 中)
 * @param mod 父模块 (用于获取 Context 和添加到链表)
 * @param name 函数名 (将被 intern)
 * @param ret_type 返回类型
 * @return 指向新函数的指针
 */
IRFunction *ir_function_create(IRModule *mod, const char *name, IRType *ret_type);

/**
 * @brief [已废弃] void ir_function_destroy(IRFunction *func);
 * 内存由 Arena 管理。
 */

/**
 * @brief 创建一个函数参数 (在 Arena 中)
 * @param func 父函数 (用于获取 Context 和添加到链表)
 * @param type 参数类型
 * @param name 参数名 (将被 intern)
 * @return 指向新参数的指针
 */
IRArgument *ir_argument_create(IRFunction *func, IRType *type, const char *name);

// --- 调试 ---
void ir_function_dump(IRFunction *func, FILE *stream);

#endif
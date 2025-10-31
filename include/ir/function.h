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
  IRModule *parent;          // <-- 指向父模块

  IRType *return_type;   // 缓存的返回类型 (e.g., i32)
  IRType *function_type; // [!!] 新增: 完整的函数类型 (e.g., i32 (i32, f64))

  IDList list_node;    // <-- 节点，用于加入 Module->functions 链表
  IDList arguments;    // 链表头 (元素是 IRArgument)
  IDList basic_blocks; // 链表头 (元素是 IRBasicBlock)
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
 * @brief [!!] API 恢复 !!
 * 创建一个新函数 (在 Arena 中), *但不* 最终确定其类型。
 *
 * @param mod 父模块
 * @param name 函数名
 * @param ret_type *仅仅* 是返回类型 (e.g., i32)
 * @return 指向新 IRFunction 的指针
 */
IRFunction *ir_function_create(IRModule *mod, const char *name, IRType *ret_type);

/**
 * @brief [!!] API 恢复 !!
 * 向一个 *未定稿* 的函数添加一个参数。
 */
IRArgument *ir_argument_create(IRFunction *func, IRType *type, const char *name);

/**
 * @brief [!!] 新增 API !!
 *
 * 在所有参数都已添加后，“定稿”函数的签名。
 * 此函数会计算完整的 IR_TYPE_FUNCTION，并*最终*设置
 * func->entry_address.type (以修复 'call' bug)。
 *
 * @param func 要定稿的函数
 * @param is_variadic 是否为可变参数
 */
void ir_function_finalize_signature(IRFunction *func, bool is_variadic);

// --- 调试 ---
void ir_function_dump(IRFunction *func, FILE *stream);

#endif
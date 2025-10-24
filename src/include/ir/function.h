#ifndef FUNCTION_H
#define FUNCTION_H

#include "ir/id_list.h"
#include "ir/module.h"
#include "ir/type.h"
#include "ir/value.h"
#include <stdio.h>

// 函数
typedef struct
{
  IRValueNode entry_address; // 函数入口地址
  IDList list_node;          // <-- 节点，用于加入 Module->functions 链表

  IRType *return_type;
  IDList arguments;    // 链表头 (元素是 IRArgument)
  IDList basic_blocks; // 链表头 (元素是 IRBasicBlock)
  IRModule *parent;
} IRFunction;

// 函数参数
typedef struct
{
  IRValueNode value;
  IDList list_node; // <-- 节点，用于加入 Function->arguments 链表
  IRFunction *parent;
} IRArgument;

/**
 * @brief 创建一个新函数，并将其附加到模块
 * @param mod 父模块
 * @param name 函数名 (将被复制)
 * @param ret_type 返回类型
 * @return 指向新函数的指针
 */
IRFunction *ir_function_create(IRModule *mod, const char *name, IRType *ret_type);

/**
 * @brief 销毁一个函数 (及其所有参数和基本块)
 */
void ir_function_destroy(IRFunction *func);

/**
 * @brief 创建一个函数参数，并将其附加到函数
 * @param func 父函数
 * @param type 参数类型
 * @param name 参数名 (将被复制)
 * @return 指向新参数的指针
 */
IRArgument *ir_argument_create(IRFunction *func, IRType *type, const char *name);

/**
 * @brief 销毁一个函数参数
 */
void ir_argument_destroy(IRArgument *arg);

// --- 调试 ---

/**
 * @brief 将函数的 IR 打印到指定的流
 * @param func 要打印的函数
 * @param stream 输出流
 */
void ir_function_dump(IRFunction *func, FILE *stream);

#endif
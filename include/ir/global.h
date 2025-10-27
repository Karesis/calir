#ifndef IR_GLOBAL_H
#define IR_GLOBAL_H

#include "ir/context.h"
#include "ir/module.h"
#include "ir/type.h"
#include "ir/value.h"
#include "utils/id_list.h"
#include <stdio.h> // For FILE

/**
 * @brief 全局变量
 *
 * 代表一个在模块级别的具名变量 (e.g., @my_global = global i32 0)
 * 它的 IRValueNode (value) 代表它的 *地址*。
 */
typedef struct IRGlobalVariable
{
  IRValueNode value; // <-- 基类 (kind = IR_KIND_GLOBAL)
                     // (value.type 将是一个 *指针* 类型, e.g., ptr)

  IDList list_node; // <-- 节点, 用于加入 Module->globals 链表
  IRModule *parent; // <-- 指向父模块

  IRType *allocated_type;   // <-- 全局变量 *自身* 的类型 (e.g., i32)
  IRValueNode *initializer; // (可选) 初始值 (必须是一个 Constant)

} IRGlobalVariable;

/**
 * @brief 创建一个新的全局变量 (在 Arena 中)
 *
 * @param mod 父模块 (用于获取 Context 和添加到链表)
 * @param name 全局变量的名字 (e.g., "my_global")
 * @param allocated_type 全局变量要分配的类型 (e.g., i32, [10 x i32], ...)
 * @param initializer (可选) 初始值。必须是一个 Constant。如果为 NULL,
 * 则默认为零初始化 (zeroinitializer)。
 * @return 指向新全局变量的指针 (其 value.type 是 allocated_type 的指针类型)
 */
IRGlobalVariable *ir_global_variable_create(IRModule *mod, const char *name, IRType *allocated_type,
                                            IRValueNode *initializer);

/**
 * @brief 将单个全局变量的 IR 打印到流
 * @param global 要打印的全局变量
 * @param stream 输出流
 */
void ir_global_variable_dump(IRGlobalVariable *global, FILE *stream);

#endif // IR_GLOBAL_H
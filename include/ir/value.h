#ifndef VALUE_H
#define VALUE_H

#include "ir/type.h"
#include "utils/id_list.h"
#include <stdio.h>

/**
 * @brief 区分 IRValueNode 到底“是”什么
 *
 * 这用于实现安全的向下转型 (down-casting)。
 * (例如，从 IRValueNode* 转换回 IRInstruction*)
 */
typedef enum
{
  IR_KIND_ARGUMENT,    // 这是一个函数参数 (IRArgument)
  IR_KIND_INSTRUCTION, // 这是一个指令的结果 (IRInstruction)
  IR_KIND_BASIC_BLOCK, // 这是一个基本块的标签 (IRBasicBlock)
  IR_KIND_FUNCTION,    // 这是一个函数地址 (IRFunction)
  IR_KIND_CONSTANT,    // 这是一个常量 (IRConstant)
  IR_KIND_GLOBAL,      // 这是一个全局变量 (IRGlobalVariable)
} IRValueKind;

/**
 * @brief 所有 IR 对象的基类
 * 代表一个可以被“使用”并具有“类型”的值。
 */
typedef struct
{
  IRValueKind kind; // <-- 用于运行时类型识别
  char *name;       // 如 %entry, %x, %tmp1， 用于调试
  IRType *type;
  IDList uses; // “谁在用我？” (Def-Use 链)
               // 这是一个 IRUse 对象的链表头
} IRValueNode;

/**
 * @brief 打印一个 Value 的引用 (e.g., "i32 %foo" or "label %entry")
 * (这是我们之前 temp_print_value 的正式版本)
 * @param val 要打印的 Value
 * @param stream 输出流
 */
void ir_value_dump(IRValueNode *val, FILE *stream);

/**
 * @brief 安全地设置 Value 的名字 (自动 free 旧名字并 strdup 新名字)
 * @param val 要修改的 Value
 * @param name 新的名字
 */
void ir_value_set_name(IRValueNode *val, const char *name);

/**
 * @brief (核心优化函数) 替换所有对 old_val 的使用为 new_val
 *
 * 遍历 old_val->uses 链表, 将每个 Use 重新指向 new_val 的 uses 链表。
 * 这是实现常量折叠、指令合并等优化的基础。
 *
 * @param old_val 将被替换的 Value
 * @param new_val 替换后的新 Value
 */
void ir_value_replace_all_uses_with(IRValueNode *old_val, IRValueNode *new_val);

#endif

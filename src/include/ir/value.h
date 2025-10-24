#ifndef VALUE_H
#define VALUE_H

#include "ir/id_list.h"
#include "ir/type.h"

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
  const char *name; // 如 %entry, %x, %tmp1， 用于调试
  IRType *type;
  IDList uses; // “谁在用我？” (Def-Use 链)
               // 这是一个 IRUse 对象的链表头
} IRValueNode;

#endif
// src/include/ir/constant.h

#ifndef IR_CONSTANT_H
#define IR_CONSTANT_H

#include "ir/value.h"
#include <stdint.h> // for int32_t

// 区分不同类型的常量
typedef enum
{
  CONST_KIND_INT,   // 整数, e.g., i32 5
  CONST_KIND_UNDEF, // 'undef' (未定义) 值
} IRConstantKind;

/**
 * @brief 代表一个常量值 (如 5 或 undef)
 * "继承" (内嵌) IRValueNode
 */
typedef struct IRConstant
{
  IRValueNode value; // 基类 (value.kind 必须是 IR_KIND_CONSTANT)
  IRConstantKind const_kind;

  // 存储常量的值
  union {
    int32_t int_val; // 用于 CONST_KIND_INT
  } data;

} IRConstant;

// --- API 函数 ---

/**
 * @brief 获取一个 'undef' 常量 (未定义值)
 *
 * 这对于安全地销毁 Value (替换所有使用) 至关重要。
 * 'undef' 值是按类型缓存的 (基本类型)。
 *
 * @param type 'undef' 值的类型 (e.g., ir_type_get_i32())
 * @return 指向 'undef' 常量的 IRValueNode*
 */
IRValueNode *ir_constant_get_undef(IRType *type);

/**
 * @brief 获取一个 i32 整数常量
 *
 * (注意: 一个完整的编译器会 "intern" (缓存) 这些常量)
 *
 * @param type 必须是 ir_type_get_i32()
 * @param value 整数的值
 * @return 指向新(malloc'd) i32 常量的 IRValueNode*
 */
IRValueNode *ir_constant_get_int(IRType *type, int32_t value);

/**
 * @brief 销毁一个常量
 *
 * (注意: 仅用于销毁 malloc'd 的常量, 如 get_int 或 get_undef(ptr))
 * (不要用它销毁缓存的静态 'undef' 常量)
 */
void ir_constant_destroy(IRConstant *konst);

#endif // IR_CONSTANT_H
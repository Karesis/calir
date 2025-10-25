#ifndef TYPE_H
#define TYPE_H

#include "ir/context.h"
#include <stddef.h> // for size_t
#include <stdio.h>  // for FILE

/**
 * @brief 扩展后的类型枚举 (以匹配 context.h)
 */
typedef enum
{
  IR_TYPE_VOID, // (void)
  IR_TYPE_I1,   // (bool)
  IR_TYPE_I8,
  IR_TYPE_I16,
  IR_TYPE_I32,
  IR_TYPE_I64,
  IR_TYPE_F32,
  IR_TYPE_F64,
  IR_TYPE_PTR,   // 指针类型
  IR_TYPE_LABEL, // 基本块标签类型
  // 未来: IR_TYPE_STRUCT, IR_TYPE_FUNCTION
} IRTypeKind;

/**
 * @brief IR 类型结构体 (保持不变)
 */
typedef struct IRType
{
  IRTypeKind kind;
  IRType *pointee_type; // 仅用于 IR_TYPE_PTR
} IRType;

/*
 * =================================================================
 * --- 内部构造函数 (Internal Constructors) ---
 * =================================================================
 *
 * 这些函数 *不* 检查缓存。
 * 它们只是在 permanent_arena 中分配并初始化一个新的类型对象。
 * 它们将由 'context.c' 中的单例和缓存 API (ir_type_get_...) 调用。
 */

/**
 * @brief [内部] 创建一个新的基本类型 (i32, void, ...)
 * @param ctx Context (用于内存分配)
 * @param kind 类型的 Kind
 * @return 指向新类型的 IRType*
 */
IRType *ir_type_create_primitive(IRContext *ctx, IRTypeKind kind);

/**
 * @brief [内部] 创建一个新的指针类型
 * @param ctx Context (用于内存分配)
 * @param pointee_type 指针所指向的类型
 * @return 指向新类型的 IRType*
 */
IRType *ir_type_create_ptr(IRContext *ctx, IRType *pointee_type);

// --- 调试 API (保持不变) ---

void ir_type_to_string(IRType *type, char *buffer, size_t size);
void ir_type_dump(IRType *type, FILE *stream);

#endif // TYPE_H
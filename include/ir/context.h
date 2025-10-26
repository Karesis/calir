#ifndef IR_CONTEXT_H
#define IR_CONTEXT_H

#include "ir/type.h" // typedef IRContext goes here
#include "ir/value.h"
#include "utils/bump.h"    // Arena 分配器
#include "utils/hashmap.h" // 哈希表主头文件
#include <stdbool.h>       // for bool
#include <stddef.h>        // for size_t
#include <stdint.h>        // for integer types

/*
 * =================================================================
 * --- IR 上下文 (IR Context) ---
 * =================================================================
 *
 * 这是 calir 的“中央管理器”对象。
 *
 * 它拥有:
 * 1. 内存分配器 (Arenas)，用于快速分配对象。
 * 2. 唯一化哈希表 (Caches)，用于确保类型和常量是唯一的。
 * 3. 单例对象 (Singletons)，如 'void' 和 'i32' 类型。
 *
 * 你的项目中所有其他 IR 结构 (Module, Function, Builder)
 * 都将持有一个指向此 IRContext 的指针。
 */

/**
 * @brief IR 上下文 (Context) 结构体定义
 */
struct IRContext
{
  // --- 1. 内存管理 (来自 bump.h) ---

  /** 永久 Arena (用于 Types, Constants, Strings, Hashes) */
  Bump permanent_arena;
  /** 临时 IR Arena (用于 Modules, Functions, Instructions, ...) */
  Bump ir_arena;

  // --- 2. 唯一化哈希表 (Caches) (来自 hashmap.h) ---

  // Type Caches
  PtrHashMap *pointer_type_cache; // Map<IRType* (pointee), IRType* (ptr)>

  // Constant Caches
  I8HashMap *i8_constant_cache;     // Map<int8_t, IRValueNode*>
  I16HashMap *i16_constant_cache;   // Map<int16_t, IRValueNode*>
  I32HashMap *i32_constant_cache;   // Map<int32_t, IRValueNode*>
  I64HashMap *i64_constant_cache;   // Map<int64_t, IRValueNode*>
  F32HashMap *f32_constant_cache;   // Map<float, IRValueNode*>
  F64HashMap *f64_constant_cache;   // Map<double, IRValueNode*>
  PtrHashMap *undef_constant_cache; // Map<IRType*, IRValueNode* (undef)>
  PtrHashMap *array_type_cache;     // Map<IRType* (elem_ty), PtrHashMap* (Map<size_t, IRType*>)>

  // String Interning
  StrHashMap *string_intern_cache; // Map<(char*, len), const char* (unique)>

  // --- 3. 缓存的单例对象 (Singleton Objects) ---
  // (这些指针指向 permanent_arena 中的对象)

  // 基本类型
  IRType *type_void;
  IRType *type_i1; // (布尔类型)
  IRType *type_i8;
  IRType *type_i16;
  IRType *type_i32;
  IRType *type_i64;
  IRType *type_f32;
  IRType *type_f64;
  IRType *type_label;

  // i1 (bool) 类型的两个常量
  IRValueNode *const_i1_true;
  IRValueNode *const_i1_false;
};

/*
 * =================================================================
 * --- 公共 API ---
 * =================================================================
 */

// --- 生命周期 (Lifecycle) ---

/**
 * @brief 创建一个新的 IRContext
 *
 * 这将初始化 Arenas, 哈希表, 和单例类型。
 * @return 指向新 Context 的指针 (必须由 ir_context_destroy 释放)
 */
IRContext *ir_context_create(void);

/**
 * @brief 销毁一个 IRContext
 *
 * 这将释放 Context 本身，以及它拥有的所有 Arenas (及其中的所有对象)。
 * @param ctx 要销毁的 Context
 */
void ir_context_destroy(IRContext *ctx);

/**
 * @brief 重置 IR Arena (临时 Arena)
 *
 * 这会销毁所有已构建的 Modules/Functions/Instructions，
 * 但保留所有 Types/Constants (它们在 permanent_arena 中)。
 * @param ctx Context
 */
void ir_context_reset_ir_arena(IRContext *ctx);

// --- API: 类型 (Types) ---

IRType *ir_type_get_void(IRContext *ctx);
IRType *ir_type_get_i1(IRContext *ctx);
IRType *ir_type_get_i8(IRContext *ctx);
IRType *ir_type_get_i16(IRContext *ctx);
IRType *ir_type_get_i32(IRContext *ctx);
IRType *ir_type_get_i64(IRContext *ctx);
IRType *ir_type_get_f32(IRContext *ctx);
IRType *ir_type_get_f64(IRContext *ctx);

/**
 * @brief 创建/获取一个指针类型 (唯一化)
 * @param ctx Context
 * @param pointee_type 指针所指向的类型
 * @return 指向 'ptr' 类型的指针
 */
IRType *ir_type_get_ptr(IRContext *ctx, IRType *pointee_type);

/**
 * @brief 创建/获取一个数组类型 (唯一化)
 * @param ctx Context
 * @param element_type 数组内部元素的类型
 * @param element_count 数组内部元素的数量
 */
IRType *ir_type_get_array(IRContext *ctx, IRType *element_type, size_t element_count);

// --- API: 常量 (Constants) ---

/**
 * @brief 获取一个 'undef' 常量 (唯一化)
 * @param ctx Context
 * @param type 'undef' 值的类型
 * @return 指向 'undef' 常量的 IRValueNode*
 */
IRValueNode *ir_constant_get_undef(IRContext *ctx, IRType *type);

/**
 * @brief 获取一个 i1 (bool) 整数常量 (唯一化)
 */
IRValueNode *ir_constant_get_i1(IRContext *ctx, bool value);

/**
 * @brief 获取一个 i8 整数常量 (唯一化)
 */
IRValueNode *ir_constant_get_i8(IRContext *ctx, int8_t value);

/**
 * @brief 获取一个 i16 整数常量 (唯一化)
 */
IRValueNode *ir_constant_get_i16(IRContext *ctx, int16_t value);

/**
 * @brief 获取一个 i32 整数常量 (唯一化)
 */
IRValueNode *ir_constant_get_i32(IRContext *ctx, int32_t value);

/**
 * @brief 获取一个 i64 整数常量 (唯一化)
 */
IRValueNode *ir_constant_get_i64(IRContext *ctx, int64_t value);

/**
 * @brief 获取一个 f32 浮点常量 (唯一化)
 * @warning Key 不能是 NaN 或 Inf (根据你的哈希表实现)
 */
IRValueNode *ir_constant_get_f32(IRContext *ctx, float value);

/**
 * @brief 获取一个 f64 浮点常量 (唯一化)
 * @warning Key 不能是 NaN 或 Inf (根据你的哈希表实现)
 */
IRValueNode *ir_constant_get_f64(IRContext *ctx, double value);

// --- API: 字符串 (String Interning) ---

/**
 * @brief 唯一化一个字符串 (String Interning)
 *
 * 获取一个指向 Arena 中永久、唯一副本的指针。
 *
 * @param ctx Context
 * @param str 要唯一化的 C 字符串 (以 '\0' 结尾)
 * @return const char* 指向永久副本的指针
 */
const char *ir_context_intern_str(IRContext *ctx, const char *str);

/**
 * @brief 唯一化一个字符串切片 (String Slice)
 *
 * @param ctx Context
 * @param str 指向字符串数据的指针
 * @param len 字符串的长度
 * @return const char* 指向永久副本的指针 (注意: 它*不*保证以 '\0' 结尾)
 */
const char *ir_context_intern_str_slice(IRContext *ctx, const char *str, size_t len);

#endif // IR_CONTEXT_H

/*
 * Copyright 2025 Karesis
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */


#ifndef TYPE_H
#define TYPE_H

#include "ir/printer.h"
#include <stdbool.h>
#include <stddef.h>

typedef struct IRContext IRContext;

/**
 * @brief 扩展后的类型枚举 (以匹配 context.h)
 */
typedef enum
{
  IR_TYPE_VOID,
  IR_TYPE_I1,
  IR_TYPE_I8,
  IR_TYPE_I16,
  IR_TYPE_I32,
  IR_TYPE_I64,
  IR_TYPE_F32,
  IR_TYPE_F64,
  IR_TYPE_LABEL,
  IR_TYPE_PTR,
  IR_TYPE_ARRAY,
  IR_TYPE_STRUCT,
  IR_TYPE_FUNCTION
} IRTypeKind;

/**
 * @brief IR 类型结构体 (保持不变)
 */
typedef struct IRType IRType;
struct IRType
{
  IRTypeKind kind;

  union {

    IRType *pointee_type;


    struct
    {
      IRType *element_type;
      size_t element_count;
    } array;


    struct
    {
      IRType **member_types;
      size_t member_count;
      const char *name;
    } aggregate;

    struct
    {
      IRType *return_type;
      IRType **param_types;
      size_t param_count;
      bool is_variadic;
    } function;
  } as;
};

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

/**
 * @brief [内部] 创建一个新的数组类型
 * @param ctx Context
 * @param element_type 数组元素的类型
 * @param element_count 数组元素的数量
 * @return 指向新类型的 IRType*
 */
IRType *ir_type_create_array(IRContext *ctx, IRType *element_type, size_t element_count);

/**
 * @brief [内部] 创建一个新的结构体类型
 * @param ctx Context
 * @param member_types 成员类型的数组 (将被拷贝)
 * @param member_count 成员的数量
 * @param name (可选) 结构体的名字 (将被 intern)
 * @return 指向新类型的 IRType*
 */
IRType *ir_type_create_struct(IRContext *ctx, IRType **member_types, size_t member_count, const char *name);

/**
 * @brief [内部] 创建一个新的函数类型
 * @param ctx Context
 * @param return_type 返回类型
 * @param param_types 参数类型的数组 (将被拷贝)
 * @param param_count 参数的数量
 * @param is_variadic 是否为可变参数
 * @return 指向新类型的 IRType*
 */
IRType *ir_type_create_function(IRContext *ctx, IRType *return_type, IRType **param_types, size_t param_count,
                                bool is_variadic);



void ir_type_dump(IRType *type, IRPrinter *p);

#endif
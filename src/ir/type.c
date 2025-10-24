// src/ir/type.c

#include "ir/type.h"
#include <stdio.h>  // for snprintf
#include <stdlib.h> // for malloc, free
#include <string.h> // for strncpy

// --- 单例 (Singleton) 实例 ---

// 'void' 类型的全局唯一定义
static IRType g_void_type = {.kind = IR_TYPE_VOID, .pointee_type = NULL};

// 'i32' 类型的全局唯一定义
static IRType g_i32_type = {.kind = IR_TYPE_I32, .pointee_type = NULL};

// --- Type Getters ---

IRType *
ir_type_get_void()
{
  return &g_void_type;
}

IRType *
ir_type_get_i32()
{
  return &g_i32_type;
}

/**
 * @brief 创建/获取一个指针类型
 *
 * 注意: 这是一个简化的实现。
 * 在一个完整的编译器中, 你应该有一个 "Type Context" (类型上下文)，
 * 它会缓存 (intern) 所有创建过的指针类型，
 * 确保两次 ir_type_get_ptr(ir_type_get_i32()) 调用返回*相同*的地址。
 *
 * 目前，我们每次都 malloc 是可以接受的。
 */
IRType *
ir_type_get_ptr(IRType *pointee_type)
{
  IRType *ptr_type = (IRType *)malloc(sizeof(IRType));
  if (!ptr_type)
  {
    return NULL; // 内存分配失败
  }

  ptr_type->kind = IR_TYPE_PTR;
  ptr_type->pointee_type = pointee_type;

  return ptr_type;
}

void
ir_type_destroy(IRType *type)
{
  if (!type)
  {
    return;
  }

  // 仅释放指针类型，因为它们是 malloc 出来的
  if (type->kind == IR_TYPE_PTR)
  {
    free(type);
  }

  // 'void' 和 'i32' 是 static 存储，不应被 free
}

// --- 调试 ---

void
ir_type_to_string(IRType *type, char *buffer, size_t size)
{
  if (!type)
  {
    strncpy(buffer, "<null_type>", size);
    if (size > 0)
      buffer[size - 1] = '\0';
    return;
  }

  switch (type->kind)
  {
  case IR_TYPE_VOID:
    strncpy(buffer, "void", size);
    break;
  case IR_TYPE_I32:
    strncpy(buffer, "i32", size);
    break;
  case IR_TYPE_PTR:
    // 我们可以只打印 "ptr"，或者递归地打印
    // 像 "ptr to i32" (但 LLVM IR 风格通常是 "i32*")
    // 让我们用一个更像 LLVM 的风格:
    if (type->pointee_type)
    {
      char pointee_str[64];
      // 递归调用 (注意：这可能会很深)
      ir_type_to_string(type->pointee_type, pointee_str, sizeof(pointee_str));
      snprintf(buffer, size, "%s*", pointee_str);
    }
    else
    {
      // 通用指针 (或 void*)
      strncpy(buffer, "ptr", size);
    }
    break;
  default:
    strncpy(buffer, "<?_type>", size);
    break;
  }
  // 确保空终止
  if (size > 0)
    buffer[size - 1] = '\0';
}

void
ir_type_dump(IRType *type, FILE *stream)
{
  char buffer[128]; // 为深度指针类型提供足够空间
  ir_type_to_string(type, buffer, sizeof(buffer));
  fprintf(stream, "%s", buffer);
}
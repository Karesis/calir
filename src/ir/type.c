#include "ir/type.h"
#include "context.h"    // 需要 IRContext 结构体
#include "utils/bump.h" // 需要 BUMP_ALLOC_ZEROED
#include <assert.h>
#include <string.h> // for snprintf, strlcat

/**
 * @brief [内部] 创建一个新的基本类型 (i32, void, ...)
 */
IRType *
ir_type_create_primitive(IRContext *ctx, IRTypeKind kind)
{
  // 基本类型不能是指针类型
  assert(kind != IR_TYPE_PTR && "Use ir_type_create_ptr for pointer types");

  // 从永久 Arena 分配并零初始化
  IRType *type = BUMP_ALLOC_ZEROED(&ctx->permanent_arena, IRType);
  if (!type)
  {
    // OOM error
    return NULL;
  }

  type->kind = kind;
  type->pointee_type = NULL; // 零初始化已完成，这里是显式说明
  return type;
}

/**
 * @brief [内部] 创建一个新的指针类型
 */
IRType *
ir_type_create_ptr(IRContext *ctx, IRType *pointee_type)
{
  assert(pointee_type != NULL && "Pointer must point to a type");

  // 从永久 Arena 分配并零初始化
  IRType *type = BUMP_ALLOC_ZEROED(&ctx->permanent_arena, IRType);
  if (!type)
  {
    // OOM error
    return NULL;
  }

  type->kind = IR_TYPE_PTR;
  type->pointee_type = pointee_type;
  return type;
}

/*
 * =================================================================
 * --- 调试 API ---
 * =================================================================
 */

// 辅助函数，用于递归打印（例如 ptr(ptr(i32))）
static void
ir_type_to_string_recursive(IRType *type, char *buffer, size_t size)
{
  if (size == 0)
    return;

  switch (type->kind)
  {
  case IR_TYPE_VOID:
    strlcat(buffer, "void", size);
    break;
  case IR_TYPE_I1:
    strlcat(buffer, "i1", size);
    break;
  case IR_TYPE_I8:
    strlcat(buffer, "i8", size);
    break;
  case IR_TYPE_I16:
    strlcat(buffer, "i16", size);
    break;
  case IR_TYPE_I32:
    strlcat(buffer, "i32", size);
    break;
  case IR_TYPE_I64:
    strlcat(buffer, "i64", size);
    break;
  case IR_TYPE_F32:
    strlcat(buffer, "f32", size);
    break;
  case IR_TYPE_F64:
    strlcat(buffer, "f64", size);
    break;
  case IR_TYPE_PTR:
    strlcat(buffer, "ptr", size);
    // (为了简洁，我们不像 LLVM 那样打印 ptr(ty))
    // 如果需要，可以取消注释下面这行
    // strlcat(buffer, "(", size);
    // ir_type_to_string_recursive(type->pointee_type, buffer, size);
    // strlcat(buffer, ")", size);
    break;
  case IR_TYPE_LABEL:
    strlcat(buffer, "label", size);
    break;
  default:
    strlcat(buffer, "?", size);
    break;
  }
}

void
ir_type_to_string(IRType *type, char *buffer, size_t size)
{
  if (size > 0)
  {
    buffer[0] = '\0'; // 确保缓冲区为空
  }
  ir_type_to_string_recursive(type, buffer, size);
}

void
ir_type_dump(IRType *type, FILE *stream)
{
  char buffer[64];
  ir_type_to_string(type, buffer, sizeof(buffer));
  fprintf(stream, "%s", buffer);
}
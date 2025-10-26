#include "ir/type.h"
#include "ir/context.h" // 需要 IRContext 结构体
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
  type->as.pointee_type = NULL; // 零初始化已完成，这里是显式说明
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
  type->as.pointee_type = pointee_type;
  return type;
}

/**
 * @brief [内部] 创建一个新的数组类型
 */
IRType *
ir_type_create_array(IRContext *ctx, IRType *element_type, size_t element_count)
{
  assert(ctx != NULL);
  assert(element_type != NULL);

  // 从永久 Arena 分配
  IRType *type = BUMP_ALLOC_ZEROED(&ctx->permanent_arena, IRType);
  if (!type)
    return NULL; // OOM

  type->kind = IR_TYPE_ARRAY;
  type->as.array.element_type = element_type;
  type->as.array.element_count = element_count;

  return type;
}

/*
 * =================================================================
 * --- 调试 API ---
 * =================================================================
 */

/**
 * @brief [内部] 安全地追加字符串到缓冲区
 *
 * @param buffer 目标缓冲区
 * @param pos_ptr 指向当前写入位置 (索引) 的指针
 * @param size 缓冲区的总大小
 * @param str 要追加的字符串
 */
static void
safe_append(char *buffer, size_t *pos_ptr, size_t size, const char *str)
{
  // 检查是否还有空间 (至少 1 字节用于 NUL 终止符)
  if (*pos_ptr >= size - 1)
  {
    return;
  }

  // 计算剩余空间
  size_t space_left = size - *pos_ptr;

  // 使用 snprintf 安全地追加
  int written = snprintf(buffer + *pos_ptr, space_left, "%s", str);

  if (written > 0)
  {
    // 更新位置
    *pos_ptr += written;
  }
}

// 辅助函数，用于递归打印（例如 ptr(ptr(i32))）
static void
ir_type_to_string_recursive(IRType *type, char *buffer, size_t *pos_ptr, size_t size)
{
  if (*pos_ptr >= size - 1)
  {
    return;
  }

  switch (type->kind)
  {
  case IR_TYPE_VOID:
    safe_append(buffer, pos_ptr, size, "void");
    break;
  case IR_TYPE_I1:
    safe_append(buffer, pos_ptr, size, "i1");
    break;
  case IR_TYPE_I8:
    safe_append(buffer, pos_ptr, size, "i8");
    break;
  case IR_TYPE_I16:
    safe_append(buffer, pos_ptr, size, "i16");
    break;
  case IR_TYPE_I32:
    safe_append(buffer, pos_ptr, size, "i32");
    break;
  case IR_TYPE_I64:
    safe_append(buffer, pos_ptr, size, "i64");
    break;
  case IR_TYPE_F32:
    safe_append(buffer, pos_ptr, size, "f32");
    break;
  case IR_TYPE_F64:
    safe_append(buffer, pos_ptr, size, "f64");
    break;
  case IR_TYPE_LABEL: // [!!] 修正
    safe_append(buffer, pos_ptr, size, "label");
    break;
  case IR_TYPE_PTR:
    safe_append(buffer, pos_ptr, size, "ptr");
    // (为了简洁，我们不像 LLVM 那样打印 ptr(ty))
    // 如果需要，可以取消注释下面这几行
    // safe_append(buffer, pos_ptr, size, "(");
    // ir_type_to_string_recursive(type->pointee_type, buffer, pos_ptr, size);
    // safe_append(buffer, pos_ptr, size, ")");
    break;

  case IR_TYPE_ARRAY:
    // 1. 打印: "["
    safe_append(buffer, pos_ptr, size, "[");

    // 2. 打印: "10" (数量)
    char count_str[32]; // (足够放下 64-bit 整数)
    snprintf(count_str, sizeof(count_str), "%zu", type->as.array.element_count);
    safe_append(buffer, pos_ptr, size, count_str);

    // 3. 打印: " x "
    safe_append(buffer, pos_ptr, size, " x ");

    // 4. 递归打印: "i32" (元素类型)
    ir_type_to_string_recursive(type->as.array.element_type, buffer, pos_ptr, size);

    // 5. 打印: "]"
    safe_append(buffer, pos_ptr, size, "]");
    break;

  default:
    safe_append(buffer, pos_ptr, size, "?");
    break;
  }
}

void
ir_type_to_string(IRType *type, char *buffer, size_t size)
{
  if (size == 0)
    return;

  size_t pos = 0;
  ir_type_to_string_recursive(type, buffer, &pos, size);

  // 确保 NUL 终止符
  if (pos >= size)
  {
    buffer[size - 1] = '\0'; // 如果截断，强制 NUL
  }
  else
  {
    buffer[pos] = '\0'; // 正常 NUL
  }
}

void
ir_type_dump(IRType *type, FILE *stream)
{
  char buffer[64];
  ir_type_to_string(type, buffer, sizeof(buffer));
  fprintf(stream, "%s", buffer);
}
#include "ir/constant.h"
#include "ir/context.h"    // 需要 IRContext
#include "ir/type.h"       // 需要 IRType
#include "ir/value.h"      // 需要 IRValueKind
#include "utils/bump.h"    // 需要 BUMP_ALLOC_ZEROED
#include "utils/id_list.h" // 需要 包含链表实现
#include <assert.h>
#include <math.h>   // for (float) conversion
#include <string.h> // for memset

/**
 * @brief [内部] 创建一个新的 'undef' 常量。
 */
IRValueNode *
ir_constant_create_undef(IRContext *ctx, IRType *type)
{
  assert(type != NULL);

  // 从永久 Arena 分配并零初始化
  IRConstant *konst = BUMP_ALLOC_ZEROED(&ctx->permanent_arena, IRConstant);
  if (!konst)
    return NULL; // OOM

  // --- 初始化基类 (IRValueNode) ---
  konst->value.kind = IR_KIND_CONSTANT;
  konst->value.type = type;
  konst->value.name = NULL; // 常量没有名字

  // [修正] 显式初始化 'uses' 链表
  list_init(&konst->value.uses);

  // --- 初始化子类 (IRConstant) ---
  konst->const_kind = CONST_KIND_UNDEF;

  return (IRValueNode *)konst;
}

/**
 * @brief [内部] 创建一个新的整数常量 (i1 到 i64)。
 */
IRValueNode *
ir_constant_create_int(IRContext *ctx, IRType *type, int64_t value)
{
  assert(type != NULL);
  assert(type->kind >= IR_TYPE_I1 && type->kind <= IR_TYPE_I64 && "Type must be an integer type");

  IRConstant *konst = BUMP_ALLOC_ZEROED(&ctx->permanent_arena, IRConstant);
  if (!konst)
    return NULL; // OOM

  // --- 初始化基类 (IRValueNode) ---
  konst->value.kind = IR_KIND_CONSTANT;
  konst->value.type = type;
  konst->value.name = NULL;

  // [修正] 显式初始化 'uses' 链表
  list_init(&konst->value.uses);

  // --- 初始化子类 (IRConstant) ---
  konst->const_kind = CONST_KIND_INT;
  konst->data.int_val = value; // 存储完整的 i64 值

  return (IRValueNode *)konst;
}

/**
 * @brief [内部] 创建一个新的浮点常量 (f32 或 f64)。
 */
IRValueNode *
ir_constant_create_float(IRContext *ctx, IRType *type, double value)
{
  assert(type != NULL);
  assert((type->kind == IR_TYPE_F32 || type->kind == IR_TYPE_F64) && "Type must be a float type");

  IRConstant *konst = BUMP_ALLOC_ZEROED(&ctx->permanent_arena, IRConstant);
  if (!konst)
    return NULL; // OOM

  // --- 初始化基类 (IRValueNode) ---
  konst->value.kind = IR_KIND_CONSTANT;
  konst->value.type = type;
  konst->value.name = NULL;

  // [修正] 显式初始化 'uses' 链表
  list_init(&konst->value.uses);

  // --- 初始化子类 (IRConstant) ---
  konst->const_kind = CONST_KIND_FLOAT;

  // 如果是 f32，我们将值截断并存为 double
  if (type->kind == IR_TYPE_F32)
  {
    konst->data.float_val = (double)((float)value);
  }
  else
  {
    konst->data.float_val = value;
  }

  return (IRValueNode *)konst;
}
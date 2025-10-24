#include "ir/constant.h"
#include "ir/type.h"

#include <assert.h>
#include <stdbool.h> // for bool
#include <stdlib.h>
#include <string.h>

// --- 静态 'undef' 缓存 ---
// 我们为 i32 缓存一个 'undef' 实例

static IRConstant g_undef_i32;
static bool g_undef_i32_init = false;

// (内部) 初始化常量基类
static void
init_constant_value(IRConstant *konst, IRType *type)
{
  konst->value.kind = IR_KIND_CONSTANT;
  konst->value.name = NULL; // 常量没有 '%' 名字 (如 %foo)
  konst->value.type = type;
  list_init(&konst->value.uses);
}

IRValueNode *
ir_constant_get_undef(IRType *type)
{
  assert(type != NULL);

  // 1. 检查 i32 缓存
  if (type->kind == IR_TYPE_I32)
  {
    if (!g_undef_i32_init)
    {
      init_constant_value(&g_undef_i32, type);
      g_undef_i32.const_kind = CONST_KIND_UNDEF;
      g_undef_i32_init = true;
    }
    return &g_undef_i32.value;
  }

  // 2. (TODO) 检查 void 缓存 (如果需要)
  if (type->kind == IR_TYPE_VOID)
  {
    // 'void' 类型没有值，所以 'undef' 在语义上不适用
    // 但如果需要，可以在这里添加
    return NULL;
  }

  // 3. 为 'ptr' 类型 malloc
  // (与 ir_type_get_ptr 逻辑一致)
  if (type->kind == IR_TYPE_PTR)
  {
    IRConstant *konst = (IRConstant *)malloc(sizeof(IRConstant));
    if (!konst)
      return NULL;

    init_constant_value(konst, type);
    konst->const_kind = CONST_KIND_UNDEF;
    return &konst->value;
  }

  // (其他类型... 暂不支持)
  assert(0 && "Unimplemented type for undef");
  return NULL;
}

IRValueNode *
ir_constant_get_int(IRType *type, int32_t value)
{
  assert(type != NULL && type->kind == IR_TYPE_I32);

  IRConstant *konst = (IRConstant *)malloc(sizeof(IRConstant));
  if (!konst)
    return NULL;

  init_constant_value(konst, type);
  konst->const_kind = CONST_KIND_INT;
  konst->data.int_val = value;

  return &konst->value;
}

void
ir_constant_destroy(IRConstant *konst)
{
  if (!konst)
    return;

  // --- 保护静态缓存 ---
  if (konst == &g_undef_i32)
  {
    // 这是一个静态缓存, 不要 free
    return;
  }

  // 确保它在被销毁前没有被使用
  assert(list_empty(&konst->value.uses) && "Constant still in use when destroyed");

  // 常量的 'name' 是 NULL, 不需要 free

  // 释放 malloc'd 的常量 (e.g., int 或 ptr-undef)
  free(konst);
}
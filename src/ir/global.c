#include "ir/global.h"

#include "ir/context.h" // 需要 ir_context_intern_str, ir_type_get_ptr
#include "ir/module.h"
#include "ir/type.h"
#include "ir/value.h"   // 需要 IR_KIND_CONSTANT
#include "utils/bump.h" // 需要 BUMP_ALLOC_ZEROED

#include <assert.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief 创建一个新的全局变量 (在 Arena 中)
 */
IRGlobalVariable *
ir_global_variable_create(IRModule *mod, const char *name, IRType *allocated_type, IRValueNode *initializer)
{
  assert(mod != NULL && "Parent module cannot be NULL");
  IRContext *ctx = mod->context; // 从父级获取 Context

  // 1. [!!] 健全性检查 (Sanity Checks)
  assert(allocated_type != NULL && allocated_type->kind != IR_TYPE_VOID);

  // 初始值 (如果提供) 必须是一个 "全局链接" 的常量
  assert(initializer == NULL || initializer->kind == IR_KIND_CONSTANT || // e.g., 10, undef
         initializer->kind == IR_KIND_FUNCTION ||                        // e.g., @my_func
         initializer->kind == IR_KIND_GLOBAL);                           // e.g., @other_global

  // 初始值 (如果提供) 类型必须匹配
  assert(initializer == NULL || initializer->type == allocated_type);

  // 2. 从 ir_arena 分配
  IRGlobalVariable *global = BUMP_ALLOC_ZEROED(&ctx->ir_arena, IRGlobalVariable);
  if (!global)
    return NULL;

  // 3. 设置子类成员
  global->parent = mod;
  global->allocated_type = allocated_type;
  global->initializer = initializer;

  // 4. [修改] 显式初始化链表
  list_init(&global->list_node);

  // 5. 初始化 IRValueNode 基类
  global->value.kind = IR_KIND_GLOBAL;
  global->value.name = ir_context_intern_str(ctx, name); // Intern 名字
  list_init(&global->value.uses);                        // 显式初始化

  // 6. [!!] 设置 Value 的类型
  // 全局变量的 "Value" 是它的 *地址*，所以它的类型是一个 *指针*
  // (这与 alloca 的行为一致)
  global->value.type = ir_type_get_ptr(ctx, allocated_type);

  // 7. 添加到父模块的全局变量链表
  list_add_tail(&mod->globals, &global->list_node);

  return global;
}

/**
 * @brief 将单个全局变量的 IR 打印到流
 */
void
ir_global_variable_dump(IRGlobalVariable *global, FILE *stream)
{
  if (!global)
  {
    fprintf(stream, "<null global>\n");
    return;
  }

  // --- [修改点] ---

  // 1. 打印名字
  // [NEW] 使用 ir_value_dump_name, 它会正确打印 "@my_global"
  ir_value_dump_name(&global->value, stream);
  fprintf(stream, " = ");

  // 2. 打印 "global" 和类型
  // [NEW] 移除 type_str[128] 缓冲区
  fprintf(stream, "global ");
  // [NEW] 直接将类型打印到流
  ir_type_dump(global->allocated_type, stream);

  // 3. 打印初始值
  if (global->initializer)
  {
    fprintf(stream, " ");
    // [NEW] 使用 ir_value_dump_with_type,
    // 它会正确打印 "123: i32" 或 "undef: <i32>"
    ir_value_dump_with_type(global->initializer, stream);
  }
  else
  {
    fprintf(stream, " zeroinitializer");
  }

  fprintf(stream, "\n");
}
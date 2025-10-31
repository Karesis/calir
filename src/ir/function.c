#include "ir/function.h"
#include "ir/basicblock.h" // 需要 basic_block_dump
#include "ir/context.h"    // <-- [新]
#include "ir/module.h"
#include "ir/type.h"
#include "ir/value.h"
#include "utils/bump.h" // <-- [新]

#include <assert.h>
#include <stdlib.h>
#include <string.h>

// --- IRArgument 实现 ---

/**
 * @brief [内部] 创建一个函数参数 (IRArgument)
 * (由 ir_function_create 在内部调用)
 */
IRArgument *
ir_argument_create(IRFunction *func, IRType *type, const char *name)
{
  assert(func != NULL && "Parent function cannot be NULL");
  IRContext *ctx = func->parent->context; // 从父级获取 Context

  // 从 ir_arena 分配
  IRArgument *arg = BUMP_ALLOC_ZEROED(&ctx->ir_arena, IRArgument);
  if (!arg)
    return NULL;

  arg->parent = func;

  // 显式初始化链表 (BUMP_ALLOC_ZEROED 不够)
  list_init(&arg->list_node);

  // 初始化 IRValueNode 基类
  arg->value.kind = IR_KIND_ARGUMENT;
  if (name && name[0] != '\0')
  {
    // 如果提供了有效名字，则 Intern 它
    arg->value.name = ir_context_intern_str(ctx, name);
  }
  else
  {
    // 否则，显式地将其设置为 NULL
    arg->value.name = NULL;
  }
  arg->value.type = type;
  list_init(&arg->value.uses); // 显式初始化

  // 添加到父函数的参数链表
  list_add_tail(&func->arguments, &arg->list_node);

  return arg;
}

// --- IRFunction 实现 ---

/**
 * @brief [新 API] 创建一个新函数 (在 Arena 中)
 */
IRFunction *
ir_function_create(IRModule *mod, const char *name, IRType *ret_type)
{
  assert(mod != NULL && ret_type != NULL);
  IRContext *ctx = mod->context;
  IRFunction *func = BUMP_ALLOC_ZEROED(&ctx->ir_arena, IRFunction);
  if (!func)
    return NULL;

  func->parent = mod;
  func->return_type = ret_type;
  func->function_type = NULL; // [!!] 尚未定稿

  list_init(&func->list_node);
  list_init(&func->arguments); // [!!] 将被 'ir_argument_create' 填充
  list_init(&func->basic_blocks);

  // 初始化 ValueNode
  func->entry_address.kind = IR_KIND_FUNCTION;
  func->entry_address.name = ir_context_intern_str(ctx, name);
  list_init(&func->entry_address.uses);

  // [!!] 关键修复: *不要* 在这里设置类型
  func->entry_address.type = NULL; // (或一个 "未定稿" 的哨兵类型)

  // 添加到模块
  list_add_tail(&mod->functions, &func->list_node);
  return func;
}

// [!!] 新增的“定稿”函数
void
ir_function_finalize_signature(IRFunction *func, bool is_variadic)
{
  assert(func != NULL && "Cannot finalize NULL function");
  assert(func->function_type == NULL && "Function signature already finalized");

  IRContext *ctx = func->parent->context;

  // 1. 统计参数数量
  size_t num_args = 0;
  IDList *iter;
  list_for_each(&func->arguments, iter)
  {
    num_args++;
  }

  // 2. 从 'arguments' 链表构建 param_types 数组 (在 permanent_arena)
  IRType **param_types = NULL;
  if (num_args > 0)
  {
    param_types = BUMP_ALLOC_SLICE(&ctx->permanent_arena, IRType *, num_args);
    if (!param_types)
      return; // OOM

    size_t i = 0;
    list_for_each(&func->arguments, iter)
    {
      IRArgument *arg = list_entry(iter, IRArgument, list_node);
      param_types[i++] = arg->value.type;
    }
  }

  // 3. 获取唯一的函数类型
  IRType *func_type = ir_type_get_function(ctx, func->return_type, param_types, num_args, is_variadic);

  // 4. 设置函数
  func->function_type = func_type;

  // 5. [!!] 核心修复 !!
  //    设置 'entry_address' (ValueNode) 的类型
  func->entry_address.type = ir_type_get_ptr(ctx, func_type);
}

/**
 * @brief ir_function_dump
 */
void
ir_function_dump(IRFunction *func, FILE *stream)
{
  if (!func)
  {
    fprintf(stream, "<null function>\n");
    return;
  }

  // --- 1. 准备 ---
  bool is_declaration = list_empty(&func->basic_blocks);
  // [MODIFIED] 移除了 char type_str[32];

  // --- 2. 打印 'declare' 或 'define' 和签名 ---
  fprintf(stream, "%s ", is_declaration ? "declare" : "define");

  // [NEW] 直接将类型打印到流, 避免缓冲区溢出
  ir_type_dump(func->return_type, stream);

  // [NEW] ir_value_dump_name 会正确打印 "@name"
  fprintf(stream, " ");
  ir_value_dump_name(&func->entry_address, stream);
  fprintf(stream, "(");

  // --- 3. 打印参数 (新规范: %name: type) ---
  IDList *arg_iter;
  int first_arg = 1;
  list_for_each(&func->arguments, arg_iter)
  {
    if (!first_arg)
    {
      fprintf(stream, ", ");
    }
    IRArgument *arg = list_entry(arg_iter, IRArgument, list_node);

    // [NEW] 委托给 ir_value_dump_with_type
    // 它会根据 IR_KIND_ARGUMENT 自动打印 "%name: type"
    ir_value_dump_with_type(&arg->value, stream);

    first_arg = 0;
  }

  // --- 4. 打印函数体 (或结束声明) ---
  if (is_declaration)
  {
    // 声明： declare ... (...)
    fprintf(stream, ")\n");
  }
  else
  {
    // 定义： define ... (...) { ... }
    fprintf(stream, ") {\n");

    // 打印所有基本块
    IDList *bb_iter;
    list_for_each(&func->basic_blocks, bb_iter)
    {
      IRBasicBlock *bb = list_entry(bb_iter, IRBasicBlock, list_node);

      // [!!] 关键依赖：
      // ir_basic_block_dump 现在必须负责打印 "$label:"
      // 和它包含的所有指令
      ir_basic_block_dump(bb, stream);
    }

    fprintf(stream, "}\n");
  }
}
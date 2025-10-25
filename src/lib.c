#include "ir/basicblock.h"
#include "ir/builder.h"
#include "ir/constant.h"
#include "ir/context.h"
#include "ir/function.h"
#include "ir/instruction.h"
#include "ir/module.h"
#include "ir/type.h"
#include <stdio.h>

/**
 * @brief 构建一个简单的测试函数
 * define i32 @test_func(i32 %x, i32 %y)
 */
static void
build_test_function(IRModule *mod)
{
  IRContext *ctx = mod->context;

  // 1. 获取基本类型
  IRType *i32_type = ir_type_get_i32(ctx);

  // 2. 创建函数签名
  // define i32 @test_func
  IRFunction *func = ir_function_create(mod, "test_func", i32_type);

  // 3. 创建函数参数
  // (i32 %x, i32 %y)
  IRArgument *arg_x = ir_argument_create(func, i32_type, "x");
  IRArgument *arg_y = ir_argument_create(func, i32_type, "y");

  // 4. 创建入口基本块
  // entry:
  IRBasicBlock *entry_bb = ir_basic_block_create(func, "entry");

  // 5. 创建 Builder 并设置插入点
  IRBuilder *builder = ir_builder_create(ctx);
  ir_builder_set_insertion_point(builder, entry_bb);

  // --- 6. 开始构建指令 ---

  // %0 = alloca i32
  IRValueNode *alloca_inst = ir_builder_create_alloca(builder, i32_type);

  // %1 = add i32 %x, i32 %y
  IRValueNode *add_res = ir_builder_create_add(builder, &arg_x->value, &arg_y->value);

  // store i32 %1, ptr %0
  ir_builder_create_store(builder, add_res, alloca_inst);

  // %2 = load i32, ptr %0
  IRValueNode *load_res = ir_builder_create_load(builder, i32_type, alloca_inst);

  // ret i32 %2
  ir_builder_create_ret(builder, load_res);

  // --- 7. 清理 Builder ---
  ir_builder_destroy(builder);
}

int
main()
{
  printf("--- Calir IR Smoke Test ---\n\n");

  // 1. 初始化 Context
  IRContext *ctx = ir_context_create();
  if (!ctx)
  {
    fprintf(stderr, "Failed to create IRContext\n");
    return 1;
  }

  // 2. 初始化 Module
  IRModule *mod = ir_module_create(ctx, "test_module");

  // 3. 构建我们的 IR
  build_test_function(mod);

  // 4. 转储 (Dump) 整个模块的 IR
  ir_module_dump(mod, stdout);

  // 5. 销毁 Context (这将释放所有 Arena 内存)
  ir_context_destroy(ctx);

  printf("\n--- Test Complete ---\n");
  return 0;
}
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
 * @brief 构建一个测试 Array 类型的函数
 * define void @test_func()
 */
static void
build_test_function(IRModule *mod)
{
  IRContext *ctx = mod->context;

  // 1. 获取基本类型
  IRType *i32_type = ir_type_get_i32(ctx);
  IRType *void_type = ir_type_get_void(ctx);

  // 2. [!! 新测试 !!] 创建数组类型

  // %array_1d = [10 x i32]
  IRType *array_1d = ir_type_get_array(ctx, i32_type, 10);

  // %array_2d = [5 x [10 x i32]]
  // (这将测试嵌套创建和缓存)
  IRType *array_2d = ir_type_get_array(ctx, array_1d, 5);

  // 3. 创建函数签名
  // define void @test_func()
  IRFunction *func = ir_function_create(mod, "test_func", void_type);

  // 4. 创建入口基本块
  IRBasicBlock *entry_bb = ir_basic_block_create(func, "entry");

  // 5. 创建 Builder
  IRBuilder *builder = ir_builder_create(ctx);
  ir_builder_set_insertion_point(builder, entry_bb);

  // 6. [!! 新测试 !!] 使用新类型 alloca

  // %0 = alloca [10 x i32]
  ir_builder_create_alloca(builder, array_1d);

  // %1 = alloca [5 x [10 x i32]]
  ir_builder_create_alloca(builder, array_2d);

  // 7. 终结者
  ir_builder_create_ret(builder, NULL); // ret void

  // 8. 清理 Builder
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
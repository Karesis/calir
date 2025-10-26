#include "ir/basicblock.h"
#include "ir/builder.h"
#include "ir/constant.h"
#include "ir/context.h"
#include "ir/function.h"
#include "ir/instruction.h"
#include "ir/module.h"
#include "ir/type.h"
#include <assert.h>
#include <stdio.h>

/**
 * @brief 构建一个测试 Struct 类型的函数
 * define void @test_func()
 */
static void
build_test_function(IRModule *mod)
{
  IRContext *ctx = mod->context;

  // 1. 获取基本类型
  IRType *i32_type = ir_type_get_i32(ctx);
  IRType *i64_type = ir_type_get_i64(ctx);
  IRType *void_type = ir_type_get_void(ctx);

  // 2. [!! 测试匿名结构体 (GenericHashMap) !!]
  IRType *anon_members[2] = {i32_type, i64_type};
  // %anon_struct_1 = { i32, i64 }
  IRType *anon_struct_1 = ir_type_get_anonymous_struct(ctx, anon_members, 2);

  // (使用一个*不同*的栈数组，但*内容*相同)
  IRType *anon_members_2[2] = {i32_type, i64_type};
  // %anon_struct_2 应该从缓存中命中，返回与 %anon_struct_1 相同的指针
  IRType *anon_struct_2 = ir_type_get_anonymous_struct(ctx, anon_members_2, 2);

  // [!!] 关键断言：验证缓存是否工作
  assert(anon_struct_1 == anon_struct_2 && "Anonymous struct caching failed!");

  // 3. [!! 测试命名结构体 (StrHashMap) !!]
  IRType *point_members[2] = {i32_type, i32_type};
  // %point_1 = type { i32, i32 }
  IRType *point_1 = ir_type_get_named_struct(ctx, "point", point_members, 2);

  // (故意使用一个不同的定义来重新获取，测试健全性检查)
  IRType *point_members_2[2] = {i32_type, i32_type};
  // %point_2 应该从缓存中命中 "point"
  IRType *point_2 = ir_type_get_named_struct(ctx, "point", point_members_2, 2);

  // [!!] 关键断言：验证缓存是否工作
  assert(point_1 == point_2 && "Named struct caching failed!");

  // 4. [!! 测试嵌套类型 !!]
  IRType *array_ty = ir_type_get_array(ctx, i64_type, 10);
  IRType *complex_members[2] = {point_1, array_ty};
  // %complex_ty = { %point, [10 x i64] }
  IRType *complex_ty = ir_type_get_anonymous_struct(ctx, complex_members, 2);

  // 5. 创建函数和 Builder
  IRFunction *func = ir_function_create(mod, "test_func", void_type);
  IRBasicBlock *entry_bb = ir_basic_block_create(func, "entry");
  IRBuilder *builder = ir_builder_create(ctx);
  ir_builder_set_insertion_point(builder, entry_bb);

  // 6. [!!] Alloca 所有新类型

  // %0 = alloca { i32, i64 }
  ir_builder_create_alloca(builder, anon_struct_1);

  // %1 = alloca %point
  ir_builder_create_alloca(builder, point_1);

  // %2 = alloca { %point, [10 x i64] }
  ir_builder_create_alloca(builder, complex_ty);

  // 7. 终结者
  ir_builder_create_ret(builder, NULL); // ret void

  // 8. 清理
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
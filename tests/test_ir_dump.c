// tests/test_ir_dump.c
#include <stdbool.h>
#include <stdio.h>
#include <string.h> // [!!] 新增: 包含 strcmp

// 包含你的 calir 项目的核心头文件
#include "ir/basicblock.h"
#include "ir/builder.h"
#include "ir/constant.h"
#include "ir/context.h"
#include "ir/function.h"
#include "ir/module.h"
#include "ir/type.h"
#include "ir/value.h"

// [!!] 新增: 包含测试框架
#include "test_utils.h"
#include "utils/bump.h"

/**
 * @brief [!!] 新增: 此测试用例的黄金标准输出
 */
const char *EXPECTED_IR = "module = \"test_indirect_call_module\"\n"
                          "\n"
                          "define i32 @add(%a: i32, %b: i32) {\n"
                          "$entry:\n"
                          "  %sum: i32 = add %a: i32, %b: i32\n"
                          "  ret %sum: i32\n"
                          "}\n"
                          "define i32 @do_operation(%func_ptr: <i32 (i32, i32)>, %x: i32, %y: i32) {\n"
                          "$entry:\n"
                          "  %result: i32 = call <i32 (i32, i32)> %func_ptr(%x: i32, %y: i32)\n"
                          "  ret %result: i32\n"
                          "}\n";

// --- 辅助函数：构建 @add(i32, i32) ---
// [!!] (此辅助函数保持不变)
IRFunction *
build_add_function(IRModule *mod)
{
  IRContext *ctx = mod->context;
  IRBuilder *builder = ir_builder_create(ctx);
  IRType *ty_i32 = ir_type_get_i32(ctx);

  IRFunction *func = ir_function_create(mod, "add", ty_i32);
  IRArgument *arg_a_s = ir_argument_create(func, ty_i32, "a");
  IRArgument *arg_b_s = ir_argument_create(func, ty_i32, "b");
  ir_function_finalize_signature(func, false);

  IRBasicBlock *bb = ir_basic_block_create(func, "entry");
  ir_function_append_basic_block(func, bb);
  ir_builder_set_insertion_point(builder, bb);

  IRValueNode *sum = ir_builder_create_add(builder, &arg_a_s->value, &arg_b_s->value, "sum");
  ir_builder_create_ret(builder, sum);

  ir_builder_destroy(builder);
  return func;
}

/**
 * @brief [!!] 新增: 测试套件函数
 */
int
test_indirect_call()
{
  SUITE_START("IR Builder: Indirect Call");

  // 1. --- 设置 ---
  Bump arena;
  bump_init(&arena); // [!!] 为 ...dump_to_string 创建 arena
  IRContext *ctx = ir_context_create();
  IRModule *mod = ir_module_create(ctx, "test_indirect_call_module");
  IRBuilder *builder = ir_builder_create(ctx);

  // 2. --- 获取类型 ---
  IRType *ty_i32 = ir_type_get_i32(ctx);

  // 3. --- [关键] 创建函数指针类型 ---
  IRType *add_param_types[2] = {ty_i32, ty_i32};
  IRType *ty_add_func = ir_type_get_function(ctx, ty_i32, add_param_types, 2, false);
  IRType *ty_func_ptr = ir_type_get_ptr(ctx, ty_add_func);

  // 4. --- 创建 "callee" 函数 @add ---
  build_add_function(mod);

  // 5. --- 创建 "caller" 函数 @do_operation ---
  IRFunction *caller_func = ir_function_create(mod, "do_operation", ty_i32);
  IRArgument *arg_ptr_s = ir_argument_create(caller_func, ty_func_ptr, "func_ptr");
  IRArgument *arg_x_s = ir_argument_create(caller_func, ty_i32, "x");
  IRArgument *arg_y_s = ir_argument_create(caller_func, ty_i32, "y");
  ir_function_finalize_signature(caller_func, false);
  IRValueNode *val_func_ptr = &arg_ptr_s->value;
  IRValueNode *val_x = &arg_x_s->value;
  IRValueNode *val_y = &arg_y_s->value;

  // 6. --- 为 "caller" 创建 'entry' 块 ---
  IRBasicBlock *bb_entry = ir_basic_block_create(caller_func, "entry");
  ir_function_append_basic_block(caller_func, bb_entry);
  ir_builder_set_insertion_point(builder, bb_entry);

  // 7. --- [关键测试] 填充 'entry' 块 ---
  IRValueNode *call_args[2] = {val_x, val_y};
  IRValueNode *result = ir_builder_create_call(builder, val_func_ptr, call_args, 2, "result");
  ir_builder_create_ret(builder, result);

  // 8. --- [!!] 自动化测试 [!!] ---
  printf("  (Dumping module to string...)\n");
  const char *dumped_str = ir_module_dump_to_string(mod, &arena);

  SUITE_ASSERT(dumped_str != NULL, "ir_module_dump_to_string() returned NULL");
  if (dumped_str)
  {
    SUITE_ASSERT(strcmp(dumped_str, EXPECTED_IR) == 0,
                 "Indirect call IR output does not match golden string.\n"
                 "\n--- [!!] EXPECTED GOLDEN [!!] ---\n%s\n"
                 "--- [!!] ACTUAL OUTPUT [!!] ---\n%s\n",
                 EXPECTED_IR, dumped_str);
  }

  // 9. --- 清理 ---
  ir_builder_destroy(builder);
  ir_context_destroy(ctx);
  bump_destroy(&arena);

  SUITE_END();
}

/**
 * @brief [!!] 新增: 统一的 main 函数
 */
int
main(int argc, char **argv)
{
  (void)argc;
  (void)argv;

  __calir_current_suite_name = "IR Builder (Indirect Call)";
  __calir_total_suites_run++;
  if (test_indirect_call() != 0)
  {
    __calir_total_suites_failed++;
  }
  TEST_SUMMARY();
}
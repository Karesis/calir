/*
 * Copyright 2025 Karesis
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */


// tests/test_interpreter.c

#include "interpreter/interpreter.h" // [!!] 核心
#include "ir_test_helpers.h"         // (用于 build_golden_ir)
#include "test_utils.h"              // (用于 SUITE_START/ASSERT/END)

// IR 组件
#include "ir/basicblock.h"
#include "ir/builder.h"
#include "ir/constant.h"
#include "ir/context.h"
#include "ir/function.h"
#include "ir/instruction.h"
#include "ir/module.h"
#include "ir/type.h"
#include "ir/value.h"

// C 标准库
#include <stdio.h>
#include <stdlib.h>

/**
 * @brief 封装测试所需的所有核心对象
 */
typedef struct TestEnv
{
  IRContext *ctx;
  IRBuilder *b;
  IRModule *mod;
  Interpreter *interp;
} TestEnv;

/**
 * @brief [Helper] 创建一套完整的测试环境
 */
static TestEnv *
setup_test_env()
{
  TestEnv *env = (TestEnv *)malloc(sizeof(TestEnv));
  env->ctx = ir_context_create();
  env->b = ir_builder_create(env->ctx);
  env->interp = interpreter_create();
  env->mod = ir_module_create(env->ctx, "test_module");
  return env;
}

/**
 * @brief [Helper] 销毁测试环境
 */
static void
teardown_test_env(TestEnv *env)
{
  interpreter_destroy(env->interp);
  ir_builder_destroy(env->b);
  ir_context_destroy(env->ctx);
  free(env);
}

/**
 * @brief [Helper] 方便的断言宏，用于检查 i32 结果
 */
#define ASSERT_I32_RESULT(result_val, expected_int)                                                                    \
  do                                                                                                                   \
  {                                                                                                                    \
    SUITE_ASSERT((result_val).kind == RUNTIME_VAL_I32, "Result kind is not RUNTIME_VAL_I32 (was %d)",                  \
                 (result_val).kind);                                                                                   \
    if ((result_val).kind == RUNTIME_VAL_I32)                                                                          \
    {                                                                                                                  \
      SUITE_ASSERT((result_val).as.val_i32 == (expected_int), "Expected result %ld, but got %ld",                      \
                   (int64_t)(expected_int), (int64_t)((result_val).as.val_i32));                                       \
    }                                                                                                                  \
  } while (0)

/**
 * @brief 测试基本的整数二元运算
 */
int
test_int_binary_ops()
{
  SUITE_START("Interpreter: Integer Binary Ops");
  TestEnv *env = setup_test_env();
  IRType *ty_i32 = ir_type_get_i32(env->ctx);

  // --- 1. 构建 IR: define i32 @test_add(i32 %a, i32 %b) ---
  IRFunction *func_add = ir_function_create(env->mod, "test_add", ty_i32);
  IRValueNode *arg_a = &ir_argument_create(func_add, ty_i32, "a")->value;
  IRValueNode *arg_b = &ir_argument_create(func_add, ty_i32, "b")->value;
  ir_function_finalize_signature(func_add, false);
  IRBasicBlock *bb = ir_basic_block_create(func_add, "entry");
  ir_function_append_basic_block(func_add, bb);
  ir_builder_set_insertion_point(env->b, bb);
  IRValueNode *res = ir_builder_create_add(env->b, arg_a, arg_b, "res");
  ir_builder_create_ret(env->b, res);

  // --- 2. 准备参数 (10 + 5) ---
  RuntimeValue rt_a;
  rt_a.kind = RUNTIME_VAL_I32;
  rt_a.as.val_i32 = 10;
  RuntimeValue rt_b;
  rt_b.kind = RUNTIME_VAL_I32;
  rt_b.as.val_i32 = 5;
  RuntimeValue *args[] = {&rt_a, &rt_b};

  // --- 3. 运行并断言 ---
  RuntimeValue result;
  bool success = interpreter_run_function(env->interp, func_add, args, 2, &result);
  SUITE_ASSERT(success, "Interpreter failed to run @test_add");
  ASSERT_I32_RESULT(result, 15); // 10 + 5 = 15

  // [!!] 您可以在这里添加更多测试 (e.g., test_sub, test_mul, test_sdiv)
  /// more ...

  teardown_test_env(env);
  SUITE_END();
}

/**
 * @brief 测试分支和 PHI 节点
 */
int
test_branch_phi_ops()
{
  SUITE_START("Interpreter: Branching & PHI");
  TestEnv *env = setup_test_env();
  IRType *ty_i32 = ir_type_get_i32(env->ctx);
  IRType *ty_i1 = ir_type_get_i1(env->ctx);
  IRValueNode *const_10 = ir_constant_get_i32(env->ctx, 10);
  IRValueNode *const_100 = ir_constant_get_i32(env->ctx, 100);
  IRValueNode *const_200 = ir_constant_get_i32(env->ctx, 200);

  // --- 1. 构建 IR: define i32 @test_if(i32 %a) ---
  /// if (%a > 10) { ret 100 } else { ret 200 }
  IRFunction *func = ir_function_create(env->mod, "test_if", ty_i32);
  IRValueNode *arg_a = &ir_argument_create(func, ty_i32, "a")->value;
  ir_function_finalize_signature(func, false);
  IRBasicBlock *bb_entry = ir_basic_block_create(func, "entry");
  IRBasicBlock *bb_then = ir_basic_block_create(func, "then");
  IRBasicBlock *bb_else = ir_basic_block_create(func, "else");
  IRBasicBlock *bb_merge = ir_basic_block_create(func, "merge");
  ir_function_append_basic_block(func, bb_entry);
  ir_function_append_basic_block(func, bb_then);
  ir_function_append_basic_block(func, bb_else);
  ir_function_append_basic_block(func, bb_merge);

  ir_builder_set_insertion_point(env->b, bb_entry);
  IRValueNode *cmp = ir_builder_create_icmp(env->b, IR_ICMP_SGT, arg_a, const_10, "cmp");
  ir_builder_create_cond_br(env->b, cmp, &bb_then->label_address, &bb_else->label_address);

  ir_builder_set_insertion_point(env->b, bb_then);
  ir_builder_create_br(env->b, &bb_merge->label_address);

  ir_builder_set_insertion_point(env->b, bb_else);
  ir_builder_create_br(env->b, &bb_merge->label_address);

  ir_builder_set_insertion_point(env->b, bb_merge);
  IRValueNode *phi = ir_builder_create_phi(env->b, ty_i32, "res");
  ir_phi_add_incoming(phi, const_100, bb_then);
  ir_phi_add_incoming(phi, const_200, bb_else);
  ir_builder_create_ret(env->b, phi);

  // --- 2. 运行测试 1 (Then path, a=15) ---
  RuntimeValue rt_a_15;
  rt_a_15.kind = RUNTIME_VAL_I32;
  rt_a_15.as.val_i32 = 15;
  RuntimeValue *args_15[] = {&rt_a_15};
  RuntimeValue result_15;
  bool success_15 = interpreter_run_function(env->interp, func, args_15, 1, &result_15);
  SUITE_ASSERT(success_15, "Interpreter failed (Then path)");
  ASSERT_I32_RESULT(result_15, 100); /// 15 > 10, 应该返回 100

  // --- 3. 运行测试 2 (Else path, a=5) ---
  RuntimeValue rt_a_5;
  rt_a_5.kind = RUNTIME_VAL_I32;
  rt_a_5.as.val_i32 = 5;
  RuntimeValue *args_5[] = {&rt_a_5};
  RuntimeValue result_5;
  bool success_5 = interpreter_run_function(env->interp, func, args_5, 1, &result_5);
  SUITE_ASSERT(success_5, "Interpreter failed (Else path)");
  ASSERT_I32_RESULT(result_5, 200); /// 5 is not > 10, 应该返回 200

  teardown_test_env(env);
  SUITE_END();
}

// --- FFI 包装器 (我们的 C ABI 合约) ---
static ExecutionResultKind
my_c_add_wrapper(ExecutionContext *ctx, RuntimeValue **args, size_t num_args, RuntimeValue *result_out)
{
  // 校验
  if (num_args != 2 || args[0]->kind != RUNTIME_VAL_I32 || args[1]->kind != RUNTIME_VAL_I32)
  {
    if (ctx)
      ctx->error_message = "FFI Error: my_c_add expects 2 i32 args";
    return EXEC_ERR_INVALID_PTR; // (用一个通用错误代替)
  }

  // 执行
  int32_t a = args[0]->as.val_i32;
  int32_t b = args[1]->as.val_i32;

  // 返回
  result_out->kind = RUNTIME_VAL_I32;
  result_out->as.val_i32 = a + b;
  return EXEC_OK;
}

/**
 * @brief 测试 FFI 调用和运行时错误
 */
int
test_ffi_and_errors()
{
  SUITE_START("Interpreter: FFI & Errors");
  TestEnv *env = setup_test_env();
  IRType *ty_i32 = ir_type_get_i32(env->ctx);

  // --- 1. 注册 FFI 函数 ---
  interpreter_register_external_function(env->interp, "my_c_add", my_c_add_wrapper);

  // --- 2. 构建 IR (declare 和 define) ---
  // declare i32 @my_c_add(i32, i32)
  IRFunction *func_decl = ir_function_create(env->mod, "my_c_add", ty_i32);
  ir_argument_create(func_decl, ty_i32, "a");
  ir_argument_create(func_decl, ty_i32, "b");
  ir_function_finalize_signature(func_decl, false);
  func_decl->is_declaration = true; // [!] 标记为 FFI

  // define i32 @test_ffi(i32 %x, i32 %y)
  IRFunction *func_ffi = ir_function_create(env->mod, "test_ffi", ty_i32);
  IRValueNode *arg_x = &ir_argument_create(func_ffi, ty_i32, "x")->value;
  IRValueNode *arg_y = &ir_argument_create(func_ffi, ty_i32, "y")->value;
  ir_function_finalize_signature(func_ffi, false);
  IRBasicBlock *bb_ffi = ir_basic_block_create(func_ffi, "entry");
  ir_function_append_basic_block(func_ffi, bb_ffi);
  ir_builder_set_insertion_point(env->b, bb_ffi);
  IRValueNode *call_args[] = {arg_x, arg_y};
  IRValueNode *call_res = ir_builder_create_call(env->b, &func_decl->entry_address, call_args, 2, "res");
  ir_builder_create_ret(env->b, call_res);

  // --- 3. 运行 FFI 测试 (70 + 7) ---
  RuntimeValue rt_x;
  rt_x.kind = RUNTIME_VAL_I32;
  rt_x.as.val_i32 = 70;
  RuntimeValue rt_y;
  rt_y.kind = RUNTIME_VAL_I32;
  rt_y.as.val_i32 = 7;
  RuntimeValue *ffi_args[] = {&rt_x, &rt_y};
  RuntimeValue ffi_result;
  bool ffi_success = interpreter_run_function(env->interp, func_ffi, ffi_args, 2, &ffi_result);
  SUITE_ASSERT(ffi_success, "Interpreter failed to run @test_ffi");
  ASSERT_I32_RESULT(ffi_result, 77); /// 70 + 7 = 77

  // --- 4. 构建并运行“未链接” FFI 测试 ---
  IRFunction *func_unlinked_decl = ir_function_create(env->mod, "unlinked_fn", ty_i32);
  ir_function_finalize_signature(func_unlinked_decl, false);
  func_unlinked_decl->is_declaration = true;

  IRFunction *func_unlinked = ir_function_create(env->mod, "test_unlinked", ty_i32);
  ir_function_finalize_signature(func_unlinked, false);
  IRBasicBlock *bb_unlinked = ir_basic_block_create(func_unlinked, "entry");
  ir_function_append_basic_block(func_unlinked, bb_unlinked);
  ir_builder_set_insertion_point(env->b, bb_unlinked);
  ir_builder_create_call(env->b, &func_unlinked_decl->entry_address, NULL, 0, "res");
  ir_builder_create_ret(env->b, NULL); // (假设 ret void)

  RuntimeValue err_result;
  bool err_success = interpreter_run_function(env->interp, func_unlinked, NULL, 0, &err_result);
  SUITE_ASSERT(!err_success, "Interpreter should have failed on unlinked FFI call");

  teardown_test_env(env);
  SUITE_END();
}

/**
 * @brief 测试运行 ir_test_helpers.h 中的 'golden IR'
 */
int
test_golden_ir_execution()
{
  SUITE_START("Interpreter: Golden IR Execution");
  TestEnv *env = setup_test_env();

  // 1. [!!] 链接 'golden IR' 需要的 FFI 函数
  interpreter_register_external_function(env->interp, "external_add", my_c_add_wrapper);

  // 2. 构建黄金 IR (来自 ir_test_helpers.h)
  IRModule *golden_mod = build_golden_ir(env->ctx, env->b);
  // (我们必须找到 kitchen_sink 函数)
  IRFunction *kitchen_sink_func = NULL;
  IDList *func_it;
  list_for_each(&golden_mod->functions, func_it)
  {
    IRFunction *f = list_entry(func_it, IRFunction, list_node);
    if (strcmp(f->entry_address.name, "kitchen_sink") == 0)
    {
      kitchen_sink_func = f;
      break;
    }
  }
  SUITE_ASSERT(kitchen_sink_func != NULL, "Failed to find @kitchen_sink in golden IR");

  /// 3. 运行测试 1 (Then path: a=15, b=5)
  /// %cmp = icmp sgt 15, 5 (true)
  /// %call_res = call @external_add(15, 10) -> 25
  /// %phi_val = 25
  RuntimeValue rt_a_15;
  rt_a_15.kind = RUNTIME_VAL_I32;
  rt_a_15.as.val_i32 = 15;
  RuntimeValue rt_b_5;
  rt_b_5.kind = RUNTIME_VAL_I32;
  rt_b_5.as.val_i32 = 5;
  RuntimeValue *args_15_5[] = {&rt_a_15, &rt_b_5};
  RuntimeValue result_15_5;
  bool success_15_5 = interpreter_run_function(env->interp, kitchen_sink_func, args_15_5, 2, &result_15_5);
  SUITE_ASSERT(success_15_5, "Golden IR failed (Then path)");
  ASSERT_I32_RESULT(result_15_5, 25);

  /// 4. 运行测试 2 (Else path: a=5, b=30)
  /// %cmp = icmp sgt 5, 30 (false)
  /// %sub_res = sub 30, 20 -> 10
  /// %phi_val = 10
  RuntimeValue rt_a_5;
  rt_a_5.kind = RUNTIME_VAL_I32;
  rt_a_5.as.val_i32 = 5;
  RuntimeValue rt_b_30;
  rt_b_30.kind = RUNTIME_VAL_I32;
  rt_b_30.as.val_i32 = 30;
  RuntimeValue *args_5_30[] = {&rt_a_5, &rt_b_30};
  RuntimeValue result_5_30;
  bool success_5_30 = interpreter_run_function(env->interp, kitchen_sink_func, args_5_30, 2, &result_5_30);
  SUITE_ASSERT(success_5_30, "Golden IR failed (Else path)");
  ASSERT_I32_RESULT(result_5_30, 10);

  teardown_test_env(env);
  SUITE_END();
}

/**
 * @brief 主测试运行器
 */
int
main()
{
  __calir_current_suite_name = "Interpreter"; // (用于 TEST_SUMMARY)

  __calir_total_suites_run++;
  if (test_int_binary_ops() != 0)
  {
    __calir_total_suites_failed++;
  }

  __calir_total_suites_run++;
  if (test_branch_phi_ops() != 0)
  {
    __calir_total_suites_failed++;
  }

  __calir_total_suites_run++;
  if (test_ffi_and_errors() != 0)
  {
    __calir_total_suites_failed++;
  }

  __calir_total_suites_run++;
  if (test_golden_ir_execution() != 0)
  {
    __calir_total_suites_failed++;
  }

  TEST_SUMMARY();
}
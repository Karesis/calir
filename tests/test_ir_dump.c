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

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "ir/basicblock.h"
#include "ir/builder.h"
#include "ir/constant.h"
#include "ir/context.h"
#include "ir/function.h"
#include "ir/module.h"
#include "ir/type.h"
#include "ir/value.h"

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

  Bump arena;
  bump_init(&arena);
  IRContext *ctx = ir_context_create();
  IRModule *mod = ir_module_create(ctx, "test_indirect_call_module");
  IRBuilder *builder = ir_builder_create(ctx);

  IRType *ty_i32 = ir_type_get_i32(ctx);

  IRType *add_param_types[2] = {ty_i32, ty_i32};
  IRType *ty_add_func = ir_type_get_function(ctx, ty_i32, add_param_types, 2, false);
  IRType *ty_func_ptr = ir_type_get_ptr(ctx, ty_add_func);

  build_add_function(mod);

  IRFunction *caller_func = ir_function_create(mod, "do_operation", ty_i32);
  IRArgument *arg_ptr_s = ir_argument_create(caller_func, ty_func_ptr, "func_ptr");
  IRArgument *arg_x_s = ir_argument_create(caller_func, ty_i32, "x");
  IRArgument *arg_y_s = ir_argument_create(caller_func, ty_i32, "y");
  ir_function_finalize_signature(caller_func, false);
  IRValueNode *val_func_ptr = &arg_ptr_s->value;
  IRValueNode *val_x = &arg_x_s->value;
  IRValueNode *val_y = &arg_y_s->value;

  IRBasicBlock *bb_entry = ir_basic_block_create(caller_func, "entry");
  ir_function_append_basic_block(caller_func, bb_entry);
  ir_builder_set_insertion_point(builder, bb_entry);

  IRValueNode *call_args[2] = {val_x, val_y};
  IRValueNode *result = ir_builder_create_call(builder, val_func_ptr, call_args, 2, "result");
  ir_builder_create_ret(builder, result);

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
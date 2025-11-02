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


/* tests/test_readme_example.c */
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "ir/basicblock.h"
#include "ir/builder.h"
#include "ir/constant.h"
#include "ir/context.h"
#include "ir/function.h"
#include "ir/global.h"
#include "ir/module.h"
#include "ir/type.h"
#include "ir/value.h"

/* * 包含测试工具
 */
#include "test_utils.h"
#include "utils/bump.h"

/**
 * @brief [!!] 这是我们新的 README 将要展示的 IR 字符串
 *
 * 它遵循新的 parser 友好格式 (module = "...", @gvar: <type> = ..., %name: type = ...)
 */
const char *EXPECTED_README_IR =
  "module = \"test_module\"\n"
  "\n"
  "%point = type { i32, i64 }\n"
  "%data_packet = type { %point, [10 x i32] }\n"
  "\n"
  "@g_data = global [10 x i32] zeroinitializer\n"
  "\n"
  "define void @test_func(%idx: i32) {\n"
  "$entry:\n"
  "  %packet_ptr: <%data_packet> = alloc %data_packet\n"
  "  %elem_ptr: <i32> = gep inbounds %packet_ptr: <%data_packet>, 0: i32, 1: i32, %idx: i32\n"
  "  store 123: i32, %elem_ptr: <i32>\n"
  "  ret void\n"
  "}\n";

/**
 * @brief [!!] 这是我们新的 README 将要展示的 C Builder API 代码
 *
 * (基于你的旧 README 示例, 但已更新,
 * 1. 删除了未使用的 'point_ptr'
 * 2. 为 alloca/gep 添加了 name hints)
 */
static void
build_readme_ir(IRModule *mod)
{
  IRContext *ctx = mod->context;


  IRType *i32_type = ir_type_get_i32(ctx);
  IRType *i64_type = ir_type_get_i64(ctx);
  IRType *void_type = ir_type_get_void(ctx);


  IRType *point_members[2] = {i32_type, i64_type};
  IRType *point_type = ir_type_get_named_struct(ctx, "point", point_members, 2);


  IRType *array_type = ir_type_get_array(ctx, i32_type, 10);


  IRType *packet_members[2] = {point_type, array_type};
  IRType *data_packet_type = ir_type_get_named_struct(ctx, "data_packet", packet_members, 2);



  ir_global_variable_create(mod,
                            "g_data",
                            array_type,
                            NULL);



  IRFunction *func = ir_function_create(mod, "test_func", void_type);
  IRArgument *arg_idx_s = ir_argument_create(func, i32_type, "idx");
  ir_function_finalize_signature(func, false);
  IRValueNode *arg_idx = &arg_idx_s->value;

  IRBasicBlock *entry_bb = ir_basic_block_create(func, "entry");
  ir_function_append_basic_block(func, entry_bb);

  IRBuilder *builder = ir_builder_create(ctx);
  ir_builder_set_insertion_point(builder, entry_bb);




  IRValueNode *packet_ptr = ir_builder_create_alloca(builder, data_packet_type, "packet_ptr");


  IRValueNode *const_0 = ir_constant_get_i32(ctx, 0);
  IRValueNode *const_1 = ir_constant_get_i32(ctx, 1);
  IRValueNode *const_123 = ir_constant_get_i32(ctx, 123);


  IRValueNode *gep_indices[] = {const_0, const_1, arg_idx};
  IRValueNode *elem_ptr = ir_builder_create_gep(builder, data_packet_type, packet_ptr, gep_indices, 3,
                                                true /* inbounds */, "elem_ptr");

  ir_builder_create_store(builder, const_123, elem_ptr);


  ir_builder_create_ret(builder, NULL);
  ir_builder_destroy(builder);
}

/**
 * @brief 测试套件：验证 README 示例
 *
 * 1. 使用 build_readme_ir() 构建 IR
 * 2. 将其转储为字符串
 * 3. 与 EXPECTED_README_IR 进行比较
 */
int
test_readme_ir_builder()
{
  SUITE_START("README Example: IR Builder");


  Bump arena;
  bump_init(&arena);
  IRContext *ctx = ir_context_create();
  IRModule *mod = ir_module_create(ctx, "test_module");


  build_readme_ir(mod);


  printf("  (Dumping module to string...)\n");
  const char *dumped_str = ir_module_dump_to_string(mod, &arena);

  SUITE_ASSERT(dumped_str != NULL, "ir_module_dump_to_string() returned NULL");
  if (dumped_str)
  {

    SUITE_ASSERT(strcmp(dumped_str, EXPECTED_README_IR) == 0,
                 "README IR output does not match golden string.\n"
                 "\n--- [!!] EXPECTED GOLDEN [!!] ---\n%s\n"
                 "--- [!!] ACTUAL OUTPUT [!!] ---\n%s\n",
                 EXPECTED_README_IR, dumped_str);
  }


  ir_context_destroy(ctx);
  bump_destroy(&arena);

  SUITE_END();
}

/**
 * @brief 运行器
 */
int
main()
{
  __calir_current_suite_name = "README Example";
  __calir_total_suites_run++;
  if (test_readme_ir_builder() != 0)
  {
    __calir_total_suites_failed++;
  }
  TEST_SUMMARY();
}
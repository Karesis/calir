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

#ifndef CALIR_TEST_HELPERS_H
#define CALIR_TEST_HELPERS_H

/*
 * =================================================================
 * --- 黄金 IR - 单一事实来源 (Golden IR - Single Source of Truth) ---
 * =================================================================
 *
 * 这个文件提供了两个函数:
 * 1. get_golden_ir_text():   返回黄金 IR 的 *字符串* 形式。
 * 2. build_golden_ir():      使用 Builder API *构建* 黄金 IR。
 *
 * test_ir_printer.c 验证 build_golden_ir() == get_golden_ir_text()。
 * test_ir_parser.c 验证 parse(get_golden_ir_text()) == get_golden_ir_text()。
 */

#include "ir/basicblock.h"
#include "ir/builder.h"
#include "ir/constant.h"
#include "ir/context.h"
#include "ir/function.h"
#include "ir/instruction.h"
#include "ir/module.h"
#include "ir/type.h"
#include "ir/value.h"

/**
 * @brief [来源 1] 黄金 IR 字符串
 * (从 test_parser.c 复制而来)
 */
static const char *
get_golden_ir_text()
{

  return "module = \"golden_module\"\n"
         "\n"
         "%my_struct = type { i32, i32 }\n"
         "\n"
         "declare i32 @external_add(%x: i32, %y: i32)\n"
         "define i32 @kitchen_sink(%a: i32, %b: i32) {\n"
         "$entry:\n"
         "  %struct_ptr: <%my_struct> = alloc %my_struct\n"
         "  %elem_ptr: <i32> = gep %struct_ptr: <%my_struct>, 0: i32, 1: i32\n"
         "  store %a: i32, %elem_ptr: <i32>\n"
         "  %loaded_val: i32 = load %elem_ptr: <i32>\n"
         "  %cmp: i1 = icmp sgt %loaded_val: i32, %b: i32\n"
         "  br %cmp: i1, $then, $else\n"
         "$then:\n"
         "  %call_res: i32 = call <i32 (i32, i32)> @external_add(%a: i32, 10: i32)\n"
         "  br $merge\n"
         "$else:\n"
         "  %sub_res: i32 = sub %b: i32, 20: i32\n"
         "  br $merge\n"
         "$merge:\n"
         "  %phi_val: i32 = phi [ %call_res: i32, $then ], [ %sub_res: i32, $else ]\n"
         "  ret %phi_val: i32\n"
         "}\n";
}

/**
 * @brief [来源 2] 黄金 IR 构建器
 * (从 test_ir_dump_golden.c 复制而来)
 *
 * @param ctx IR Context
 * @param builder IR Builder
 * @return IRModule* 指向新构建的模块
 */
static __attribute__((unused)) IRModule *
build_golden_ir(IRContext *ctx, IRBuilder *builder)
{

  IRModule *mod = ir_module_create(ctx, "golden_module");

  IRType *ty_i32 = ir_type_get_i32(ctx);
  IRType *members[2] = {ty_i32, ty_i32};
  IRType *ty_struct = ir_type_get_named_struct(ctx, "my_struct", members, 2);

  IRFunction *callee = ir_function_create(mod, "external_add", ty_i32);
  ir_argument_create(callee, ty_i32, "x");
  ir_argument_create(callee, ty_i32, "y");
  ir_function_finalize_signature(callee, false);
  callee->is_declaration = true;
  callee->c_host_func_ptr = NULL;
  IRValueNode *callee_val = &callee->entry_address;

  IRValueNode *const_0 = ir_constant_get_i32(ctx, 0);
  IRValueNode *const_1 = ir_constant_get_i32(ctx, 1);
  IRValueNode *const_10 = ir_constant_get_i32(ctx, 10);
  IRValueNode *const_20 = ir_constant_get_i32(ctx, 20);

  IRFunction *func = ir_function_create(mod, "kitchen_sink", ty_i32);
  IRArgument *arg_a_s = ir_argument_create(func, ty_i32, "a");
  IRArgument *arg_b_s = ir_argument_create(func, ty_i32, "b");
  ir_function_finalize_signature(func, false);
  IRValueNode *arg_a = &arg_a_s->value;
  IRValueNode *arg_b = &arg_b_s->value;

  IRBasicBlock *bb_entry = ir_basic_block_create(func, "entry");
  IRBasicBlock *bb_then = ir_basic_block_create(func, "then");
  IRBasicBlock *bb_else = ir_basic_block_create(func, "else");
  IRBasicBlock *bb_merge = ir_basic_block_create(func, "merge");
  ir_function_append_basic_block(func, bb_entry);
  ir_function_append_basic_block(func, bb_then);
  ir_function_append_basic_block(func, bb_else);
  ir_function_append_basic_block(func, bb_merge);

  ir_builder_set_insertion_point(builder, bb_entry);
  IRValueNode *struct_ptr = ir_builder_create_alloca(builder, ty_struct, "struct_ptr");
  IRValueNode *indices[2] = {const_0, const_1};
  IRValueNode *elem_ptr = ir_builder_create_gep(builder, ty_struct, struct_ptr, indices, 2, false, "elem_ptr");
  ir_builder_create_store(builder, arg_a, elem_ptr);
  IRValueNode *loaded_val = ir_builder_create_load(builder, elem_ptr, "loaded_val");
  IRValueNode *cmp = ir_builder_create_icmp(builder, IR_ICMP_SGT, loaded_val, arg_b, "cmp");
  ir_builder_create_cond_br(builder, cmp, &bb_then->label_address, &bb_else->label_address);

  ir_builder_set_insertion_point(builder, bb_then);
  IRValueNode *call_args[2] = {arg_a, const_10};
  IRValueNode *call_res = ir_builder_create_call(builder, callee_val, call_args, 2, "call_res");
  ir_builder_create_br(builder, &bb_merge->label_address);

  ir_builder_set_insertion_point(builder, bb_else);
  IRValueNode *sub_res = ir_builder_create_sub(builder, arg_b, const_20, "sub_res");
  ir_builder_create_br(builder, &bb_merge->label_address);

  ir_builder_set_insertion_point(builder, bb_merge);
  IRValueNode *phi = ir_builder_create_phi(builder, ty_i32, "phi_val");
  ir_phi_add_incoming(phi, call_res, bb_then);
  ir_phi_add_incoming(phi, sub_res, bb_else);
  ir_builder_create_ret(builder, phi);

  return mod;
}

#endif
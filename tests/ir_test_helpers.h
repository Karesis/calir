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

// 包含所有 IR 构建所需的头文件
#include "ir/basicblock.h"
#include "ir/builder.h"
#include "ir/constant.h"
#include "ir/context.h"
#include "ir/function.h"
#include "ir/instruction.h" // [!!] 需要这个, 因为 builder API 依赖它 (e.g. IR_ICMP_SGT)
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
  // (这是你提供的黄金 IR 字符串)
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
static IRModule *__attribute__((unused))
build_golden_ir(IRContext *ctx, IRBuilder *builder)
{
  // 1. --- 设置 ---
  IRModule *mod = ir_module_create(ctx, "golden_module");

  // 2. --- 定义类型 ---
  IRType *ty_i32 = ir_type_get_i32(ctx);
  IRType *members[2] = {ty_i32, ty_i32};
  IRType *ty_struct = ir_type_get_named_struct(ctx, "my_struct", members, 2);

  // 3. --- 声明一个外部函数 (用于 call) ---
  IRFunction *callee = ir_function_create(mod, "external_add", ty_i32);
  ir_argument_create(callee, ty_i32, "x");
  ir_argument_create(callee, ty_i32, "y");
  ir_function_finalize_signature(callee, false);
  IRValueNode *callee_val = &callee->entry_address;

  // 4. --- 获取常量 ---
  IRValueNode *const_0 = ir_constant_get_i32(ctx, 0);
  IRValueNode *const_1 = ir_constant_get_i32(ctx, 1);
  IRValueNode *const_10 = ir_constant_get_i32(ctx, 10);
  IRValueNode *const_20 = ir_constant_get_i32(ctx, 20);

  // 5. --- 创建函数 @kitchen_sink(i32 %a, i32 %b) ---
  IRFunction *func = ir_function_create(mod, "kitchen_sink", ty_i32);
  IRArgument *arg_a_s = ir_argument_create(func, ty_i32, "a");
  IRArgument *arg_b_s = ir_argument_create(func, ty_i32, "b");
  ir_function_finalize_signature(func, false);
  IRValueNode *arg_a = &arg_a_s->value;
  IRValueNode *arg_b = &arg_b_s->value;

  // 6. --- 创建基本块 ---
  IRBasicBlock *bb_entry = ir_basic_block_create(func, "entry");
  IRBasicBlock *bb_then = ir_basic_block_create(func, "then");
  IRBasicBlock *bb_else = ir_basic_block_create(func, "else");
  IRBasicBlock *bb_merge = ir_basic_block_create(func, "merge");
  ir_function_append_basic_block(func, bb_entry);
  ir_function_append_basic_block(func, bb_then);
  ir_function_append_basic_block(func, bb_else);
  ir_function_append_basic_block(func, bb_merge);

  // 7. --- 填充 'entry' 块 ---
  ir_builder_set_insertion_point(builder, bb_entry);
  IRValueNode *struct_ptr = ir_builder_create_alloca(builder, ty_struct, "struct_ptr");
  IRValueNode *indices[2] = {const_0, const_1};
  IRValueNode *elem_ptr = ir_builder_create_gep(builder, ty_struct, struct_ptr, indices, 2, false, "elem_ptr");
  ir_builder_create_store(builder, arg_a, elem_ptr);
  IRValueNode *loaded_val = ir_builder_create_load(builder, elem_ptr, "loaded_val");
  IRValueNode *cmp = ir_builder_create_icmp(builder, IR_ICMP_SGT, loaded_val, arg_b, "cmp");
  ir_builder_create_cond_br(builder, cmp, &bb_then->label_address, &bb_else->label_address);

  // 8. --- 填充 'then' 块 ---
  ir_builder_set_insertion_point(builder, bb_then);
  IRValueNode *call_args[2] = {arg_a, const_10};
  IRValueNode *call_res = ir_builder_create_call(builder, callee_val, call_args, 2, "call_res");
  ir_builder_create_br(builder, &bb_merge->label_address);

  // 9. --- 填充 'else' 块 ---
  ir_builder_set_insertion_point(builder, bb_else);
  IRValueNode *sub_res = ir_builder_create_sub(builder, arg_b, const_20, "sub_res");
  ir_builder_create_br(builder, &bb_merge->label_address);

  // 10. --- 填充 'merge' 块 ---
  ir_builder_set_insertion_point(builder, bb_merge);
  IRValueNode *phi = ir_builder_create_phi(builder, ty_i32, "phi_val");
  ir_phi_add_incoming(phi, call_res, bb_then);
  ir_phi_add_incoming(phi, sub_res, bb_else);
  ir_builder_create_ret(builder, phi);

  return mod;
}

#endif // CALIR_TEST_HELPERS_H
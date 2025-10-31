#include <stdbool.h>
#include <stdio.h>

// 包含你的 calir 项目的核心头文件
#include "ir/basicblock.h"
#include "ir/builder.h"
#include "ir/constant.h"
#include "ir/context.h"
#include "ir/function.h"
#include "ir/module.h"
#include "ir/type.h"
#include "ir/value.h"

int
main(int argc, char **argv)
{
  printf("--- Calir Golden IR Dump (V2 - FINAL) ---\n");

  // 1. --- 设置 ---
  IRContext *ctx = ir_context_create();
  IRModule *mod = ir_module_create(ctx, "golden_module");
  IRBuilder *builder = ir_builder_create(ctx);

  // 2. --- 定义类型 ---
  IRType *ty_i32 = ir_type_get_i32(ctx);
  // 结构体
  IRType *members[2] = {ty_i32, ty_i32};
  IRType *ty_struct = ir_type_get_named_struct(ctx, "my_struct", members, 2);
  // 函数指针 (用于 call)
  IRType *call_params[2] = {ty_i32, ty_i32};
  IRType *ty_call_func = ir_type_get_function(ctx, ty_i32, call_params, 2, false);
  IRType *ty_call_func_ptr = ir_type_get_ptr(ctx, ty_call_func);

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

  // %struct_ptr: <%my_struct> = alloca %my_struct
  IRValueNode *struct_ptr = ir_builder_create_alloca(builder, ty_struct, "struct_ptr");

  // [!!] GEP 测试 (应打印: gep %struct_ptr: ...)
  IRValueNode *indices[2] = {const_0, const_1};
  IRValueNode *elem_ptr = ir_builder_create_gep(builder, ty_struct, struct_ptr, indices, 2, false, "elem_ptr");

  // [!!] STORE 测试
  ir_builder_create_store(builder, arg_a, elem_ptr);

  // [!!] LOAD 测试 (应打印: load %elem_ptr: ...)
  IRValueNode *loaded_val = ir_builder_create_load(builder, elem_ptr, "loaded_val");

  // [!!] ICMP 测试 (应打印: icmp sgt %loaded_val: i32, %b: i32)
  IRValueNode *cmp = ir_builder_create_icmp(builder, IR_ICMP_SGT, loaded_val, arg_b, "cmp");

  // [!!] BR 测试
  ir_builder_create_cond_br(builder, cmp, &bb_then->label_address, &bb_else->label_address);

  // 8. --- 填充 'then' 块 ---
  ir_builder_set_insertion_point(builder, bb_then);
  // [!!] CALL 测试 (应打印: call <i32(i32,i32)> @external_add(...) )
  IRValueNode *call_args[2] = {arg_a, const_10};
  IRValueNode *call_res = ir_builder_create_call(builder, callee_val, call_args, 2, "call_res");
  ir_builder_create_br(builder, &bb_merge->label_address);

  // 9. --- 填充 'else' 块 ---
  ir_builder_set_insertion_point(builder, bb_else);
  IRValueNode *sub_res = ir_builder_create_sub(builder, arg_b, const_20, "sub_res");
  ir_builder_create_br(builder, &bb_merge->label_address);

  // 10. --- 填充 'merge' 块 ---
  ir_builder_set_insertion_point(builder, bb_merge);

  // [!!] PHI 测试
  IRValueNode *phi = ir_builder_create_phi(builder, ty_i32, "phi_val");
  ir_phi_add_incoming(phi, call_res, bb_then); // [!!] 使用 then 块的结果
  ir_phi_add_incoming(phi, sub_res, bb_else);  // [!!] 使用 else 块的结果

  // [!!] RET 测试
  ir_builder_create_ret(builder, phi);

  // 11. --- 打印模块 ---
  printf("--- ir_module_dump() output: ---\n");
  ir_module_dump(mod, stdout);
  printf("----------------------------------\n");

  // 12. --- 清理 ---
  ir_builder_destroy(builder);
  ir_context_destroy(ctx);

  printf("--- Test Finished ---\n");
  return 0;
}
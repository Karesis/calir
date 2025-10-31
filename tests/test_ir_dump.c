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

// --- 辅助函数：构建 @add(i32, i32) ---
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

int
main(int argc, char **argv)
{
  printf("--- Calir IR Builder Test [indirect call] ---\"");

  // 1. --- 设置 ---
  IRContext *ctx = ir_context_create();
  IRModule *mod = ir_module_create(ctx, "test_indirect_call_module");
  IRBuilder *builder = ir_builder_create(ctx);

  // 2. --- 获取类型 ---
  IRType *ty_i32 = ir_type_get_i32(ctx);

  // 3. --- [关键] 创建函数指针类型 ---
  // 3.1. 创建函数类型: i32 (i32, i32)
  IRType *add_param_types[2] = {ty_i32, ty_i32};
  IRType *ty_add_func = ir_type_get_function(ctx, ty_i32, add_param_types, 2, false /*is_variadic*/);
  // 3.2. 创建指向该函数类型的指针: ptr
  IRType *ty_func_ptr = ir_type_get_ptr(ctx, ty_add_func);

  // 4. --- 创建 "callee" 函数 @add ---
  // (我们不需要 'add_func' 的返回值，它已经被添加到 mod->functions 链表了)
  build_add_function(mod);

  // 5. --- 创建 "caller" 函数 @do_operation ---
  // @do_operation(ptr %func_ptr, i32 %x, i32 %y)
  IRFunction *caller_func = ir_function_create(mod, "do_operation", ty_i32);
  // 5.1. 添加参数
  IRArgument *arg_ptr_s = ir_argument_create(caller_func, ty_func_ptr, "func_ptr");
  IRArgument *arg_x_s = ir_argument_create(caller_func, ty_i32, "x");
  IRArgument *arg_y_s = ir_argument_create(caller_func, ty_i32, "y");
  ir_function_finalize_signature(caller_func, false);

  // 获取参数的 ValueNode
  IRValueNode *val_func_ptr = &arg_ptr_s->value;
  IRValueNode *val_x = &arg_x_s->value;
  IRValueNode *val_y = &arg_y_s->value;

  // 6. --- 为 "caller" 创建 'entry' 块 ---
  IRBasicBlock *bb_entry = ir_basic_block_create(caller_func, "entry");
  ir_function_append_basic_block(caller_func, bb_entry);
  ir_builder_set_insertion_point(builder, bb_entry);

  // 7. --- [关键测试] 填充 'entry' 块 ---
  IRValueNode *call_args[2] = {val_x, val_y};

  // %result = call %func_ptr(i32 %x, i32 %y)
  //
  // [!!] 关键测试点:
  // callee (第一个参数) 是 'val_func_ptr' (一个 IR_KIND_ARGUMENT)
  // 而不是一个 IR_KIND_FUNCTION。
  IRValueNode *result = ir_builder_create_call(builder,
                                               val_func_ptr, // [!!] 间接调用
                                               call_args, 2, "result");

  // ret i32 %result
  ir_builder_create_ret(builder, result);

  // 8. --- 打印模块 ---
  printf("--- ir_module_dump() output: ---\n");
  ir_module_dump(mod, stdout);
  printf("----------------------------------\n");

  // 9. --- 清理 ---
  ir_builder_destroy(builder);
  ir_context_destroy(ctx);

  printf("--- Test Finished ---\n");
  return 0;
}
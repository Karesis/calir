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
 * @brief 构建一个简单的测试函数
 * define i32 @test_func(i32 %x, i32 %y)
 */
static void
build_test_function(IRModule *mod)
{
  IRContext *ctx = mod->context;

  // 1. 获取基本类型
  IRType *i32_type = ir_type_get_i32(ctx);

  // 2. 创建函数签名
  // define i32 @test_func
  IRFunction *func = ir_function_create(mod, "test_func", i32_type);

  // 3. 创建函数参数
  // (i32 %x, i32 %y)
  IRArgument *arg_x = ir_argument_create(func, i32_type, "x");
  IRArgument *arg_y = ir_argument_create(func, i32_type, "y");

  // 4. 创建入口基本块
  // entry:
  IRBasicBlock *entry_bb = ir_basic_block_create(func, "entry");
  IRBasicBlock *if_true_bb = ir_basic_block_create(func, "if_true");
  IRBasicBlock *if_false_bb = ir_basic_block_create(func, "if_false");

  // 5. 创建 Builder 并设置插入点
  IRBuilder *builder = ir_builder_create(ctx);
  ir_builder_set_insertion_point(builder, entry_bb);

  // --- 6. 开始构建指令 ---

  // %0 = icmp eq i32 %x, %y
  IRValueNode *cond = ir_builder_create_icmp(builder, IR_ICMP_EQ, &arg_x->value, &arg_y->value);

  // br i1 %0, label %if_true, label %if_false
  // (注意：我们需要传递 BasicBlock 的 ValueNode)
  ir_builder_create_cond_br(builder, cond, &if_true_bb->label_address, &if_false_bb->label_address);

  // --- 7. 填充 'if_true' 块 ---
  ir_builder_set_insertion_point(builder, if_true_bb);

  // (从 context 获取常量 1)
  IRValueNode *const_1 = ir_constant_get_i32(ctx, 1);
  // ret i32 1
  ir_builder_create_ret(builder, const_1);

  // --- 8. 填充 'if_false' 块 ---
  ir_builder_set_insertion_point(builder, if_false_bb);

  // (从 context 获取常量 0)
  IRValueNode *const_0 = ir_constant_get_i32(ctx, 0);
  // ret i32 0
  ir_builder_create_ret(builder, const_0);

  // --- 9. 清理 Builder ---
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
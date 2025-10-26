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
 * @brief 构建一个带 loop (phi) 的测试函数
 * define i32 @test_func()
 */
static void
build_test_function(IRModule *mod)
{
  IRContext *ctx = mod->context;

  // 1. 获取基本类型和常量
  IRType *i32_type = ir_type_get_i32(ctx);
  IRValueNode *const_0 = ir_constant_get_i32(ctx, 0);
  IRValueNode *const_1 = ir_constant_get_i32(ctx, 1);
  IRValueNode *const_10 = ir_constant_get_i32(ctx, 10);

  // 2. 创建函数签名
  IRFunction *func = ir_function_create(mod, "test_func", i32_type);
  // (这次没有参数)

  // 3. [!!] 创建所有基本块
  IRBasicBlock *entry_bb = ir_basic_block_create(func, "entry");
  IRBasicBlock *loop_header_bb = ir_basic_block_create(func, "loop_header");
  IRBasicBlock *loop_body_bb = ir_basic_block_create(func, "loop_body");
  IRBasicBlock *after_loop_bb = ir_basic_block_create(func, "after_loop");

  // 4. 创建 Builder
  IRBuilder *builder = ir_builder_create(ctx);

  // --- 5. 填充 'entry' 块 ---
  ir_builder_set_insertion_point(builder, entry_bb);
  // br label %loop_header
  ir_builder_create_br(builder, &loop_header_bb->label_address);

  // --- 6. 填充 'loop_header' 块 ---
  ir_builder_set_insertion_point(builder, loop_header_bb);

  // %i.1 = phi i32 ... (暂时为空)
  IRValueNode *phi_i = ir_builder_create_phi(builder, i32_type);

  // %cond = icmp slt i32 %i.1, 10
  IRValueNode *cond = ir_builder_create_icmp(builder,
                                             IR_ICMP_SLT, // 有符号小于
                                             phi_i, const_10);

  // br i1 %cond, label %loop_body, label %after_loop
  ir_builder_create_cond_br(builder, cond, &loop_body_bb->label_address, &after_loop_bb->label_address);

  // --- 7. 填充 'loop_body' 块 ---
  ir_builder_set_insertion_point(builder, loop_body_bb);

  // %i.2 = add i32 %i.1, 1
  IRValueNode *next_i = ir_builder_create_add(builder, phi_i, const_1);

  // br label %loop_header
  ir_builder_create_br(builder, &loop_header_bb->label_address);

  // --- 8. 填充 'after_loop' 块 ---
  ir_builder_set_insertion_point(builder, after_loop_bb);

  // ret i32 %i.1
  ir_builder_create_ret(builder, phi_i);

  // --- 9. [!! 关键 !!] 回填 PHI 节点 ---
  // 现在 %next_i (%i.2) 已经定义了, 我们可以安全地添加 incoming

  // %i.1 = phi i32 [ 0, %entry ], [ %i.2, %loop_body ]
  ir_phi_add_incoming(phi_i, const_0, entry_bb);
  ir_phi_add_incoming(phi_i, next_i, loop_body_bb);

  // --- 10. 清理 Builder ---
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
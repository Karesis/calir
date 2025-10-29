#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h> // for strcmp

// 包含所有 IR 核心组件
#include "ir/builder.h"
#include "ir/context.h"
#include "ir/function.h"
#include "ir/module.h"
#include "ir/type.h"
#include "ir/value.h"

// 包含所有分析 Pass
#include "analysis/cfg.h"
#include "analysis/dom_frontier.h"
#include "analysis/dom_tree.h"

// 包含我们要测试的 Pass
#include "transforms/mem2reg.h"

// [!!] 包含解释器
#include "interpreter/interpreter.h"

/**
 * @brief 构建一个菱形控制流 (if-then-else)
 * (这是从 test_mem2reg.c 复制过来的)
 *
 * define i32 @test_mem2reg() {
 * entry:
 * %x.ptr = alloca i32
 * store i32 10, ptr %x.ptr
 * br label %cond
 *
 * cond:
 * ; (%c = icmp eq i32 10, 10) -- 在这个简单测试中，我们用 i1 true
 * br i1 true, label %then, label %else
 *
 * then:
 * store i32 20, ptr %x.ptr
 * br label %merge
 *
 * else:
 * store i32 30, ptr %x.ptr
 * br label %merge
 *
 * merge:
 * %res = load i32, ptr %x.ptr
 * ret i32 %res
 * }
 */
static IRFunction *
build_test_function(IRModule *mod)
{
  IRContext *ctx = mod->context;
  IRType *i32_type = ir_type_get_i32(ctx);
  IRType *i1_type = ir_type_get_i1(ctx);

  IRFunction *func = ir_function_create(mod, "test_mem2reg", i32_type);

  // --- 创建基本块 ---
  IRBasicBlock *bb_entry = ir_basic_block_create(func, "entry");
  IRBasicBlock *bb_cond = ir_basic_block_create(func, "cond");
  IRBasicBlock *bb_then = ir_basic_block_create(func, "then");
  IRBasicBlock *bb_else = ir_basic_block_create(func, "else");
  IRBasicBlock *bb_merge = ir_basic_block_create(func, "merge");

  // --- 获取常量 ---
  IRValueNode *const_10 = ir_constant_get_i32(ctx, 10);
  IRValueNode *const_20 = ir_constant_get_i32(ctx, 20);
  IRValueNode *const_30 = ir_constant_get_i32(ctx, 30);
  IRValueNode *const_true = ir_constant_get_i1(ctx, true);

  IRBuilder *builder = ir_builder_create(ctx);

  // --- entry 块 ---
  ir_builder_set_insertion_point(builder, bb_entry);
  IRValueNode *x_ptr = ir_builder_create_alloca(builder, i32_type);
  ir_value_set_name(x_ptr, "x.ptr");
  ir_builder_create_store(builder, const_10, x_ptr);
  ir_builder_create_br(builder, &bb_cond->label_address);

  // --- cond 块 ---
  ir_builder_set_insertion_point(builder, bb_cond);
  ir_builder_create_cond_br(builder, const_true, &bb_then->label_address, &bb_else->label_address);

  // --- then 块 ---
  ir_builder_set_insertion_point(builder, bb_then);
  ir_builder_create_store(builder, const_20, x_ptr);
  ir_builder_create_br(builder, &bb_merge->label_address);

  // --- else 块 ---
  ir_builder_set_insertion_point(builder, bb_else);
  ir_builder_create_store(builder, const_30, x_ptr);
  ir_builder_create_br(builder, &bb_merge->label_address);

  // --- merge 块 ---
  ir_builder_set_insertion_point(builder, bb_merge);
  IRValueNode *res = ir_builder_create_load(builder, i32_type, x_ptr);
  ir_value_set_name(res, "res");
  ir_builder_create_ret(builder, res);

  ir_builder_destroy(builder);
  return func;
}

int
main(void)
{
  printf("--- Running test_interpreter ---\n");

  IRContext *ctx = ir_context_create();
  IRModule *mod = ir_module_create(ctx, "test_interp_module");
  IRFunction *func = build_test_function(mod);

  printf("--- 1. IR Before mem2reg ---\n");
  ir_function_dump(func, stdout);

  // --- 2. 运行 mem2reg Pass 链 ---
  Bump analysis_arena;
  bump_init(&analysis_arena);

  FunctionCFG *cfg = cfg_build(func, &analysis_arena);
  assert(cfg && "CFG build failed");
  DominatorTree *dt = dom_tree_build(cfg, &analysis_arena);
  assert(dt && "DomTree build failed");
  DominanceFrontier *df = ir_analysis_dom_frontier_compute(dt, &analysis_arena);
  assert(df && "DomFrontier build failed");

  printf("\n--- 2. Running mem2reg Pass ---\n");
  bool changed = ir_transform_mem2reg_run(func, dt, df);
  assert(changed == true && "mem2reg reported no changes");

  printf("\n--- 3. IR After mem2reg (Ready for Interpreter) ---\n");
  ir_function_dump(func, stdout);

  // --- 4. 运行解释器 ---
  printf("\n--- 4. Running Interpreter ---\n");
  Interpreter *interp = interpreter_create();
  assert(interp != NULL && "Interpreter creation failed");

  RuntimeValue result; // 用于存储返回值

  // 函数没有参数，所以传 NULL
  bool run_ok = interpreter_run_function(interp, func, NULL, 0, &result);

  printf("Interpreter run finished.\n");

  // --- 5. 验证结果 ---
  assert(run_ok == true && "Interpreter failed to run");

  // 检查返回类型
  assert(result.kind == RUNTIME_VAL_I32 && "Interpreter did not return i32");

  // 检查返回值
  printf("Interpreter returned value: %d\n", result.as.val_i32);
  assert(result.as.val_i32 == 20 && "Interpreter returned wrong value! (Expected 20)");

  printf("Interpreter result check PASSED.\n");

  // --- 6. 清理 ---
  printf("\n--- 5. Cleaning up ---\n");
  interpreter_destroy(interp);
  cfg_destroy(cfg);
  bump_destroy(&analysis_arena);
  ir_context_destroy(ctx);

  printf("\n--- test_interpreter PASSED ---\n");
  return 0;
}
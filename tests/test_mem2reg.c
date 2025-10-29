#include <assert.h>
#include <stdbool.h>
#include <stdio.h>

// 包含所有 IR 核心组件
#include "ir/builder.h"
#include "ir/context.h"
#include "ir/function.h"
#include "ir/module.h"
#include "ir/type.h"
#include "ir/use.h"
#include "ir/value.h"
#include "ir/verifier.h" // [!!] 假设我们用它来做最后的检查

// 包含所有分析 Pass
#include "analysis/cfg.h"
#include "analysis/dom_frontier.h"
#include "analysis/dom_tree.h"

// 包含我们要测试的 Pass
#include "transforms/mem2reg.h"

/**
 * @brief 构建一个菱形控制流 (if-then-else)
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
  // %x.ptr = alloca i32
  IRValueNode *x_ptr = ir_builder_create_alloca(builder, i32_type);
  ir_value_set_name(x_ptr, "x.ptr");
  // store i32 10, ptr %x.ptr
  ir_builder_create_store(builder, const_10, x_ptr);
  // br label %cond
  ir_builder_create_br(builder, &bb_cond->label_address);

  // --- cond 块 ---
  ir_builder_set_insertion_point(builder, bb_cond);
  // br i1 true, label %then, label %else
  ir_builder_create_cond_br(builder, const_true, &bb_then->label_address, &bb_else->label_address);

  // --- then 块 ---
  ir_builder_set_insertion_point(builder, bb_then);
  // store i32 20, ptr %x.ptr
  ir_builder_create_store(builder, const_20, x_ptr);
  // br label %merge
  ir_builder_create_br(builder, &bb_merge->label_address);

  // --- else 块 ---
  ir_builder_set_insertion_point(builder, bb_else);
  // store i32 30, ptr %x.ptr
  ir_builder_create_store(builder, const_30, x_ptr);
  // br label %merge
  ir_builder_create_br(builder, &bb_merge->label_address);

  // --- merge 块 ---
  ir_builder_set_insertion_point(builder, bb_merge);
  // %res = load i32, ptr %x.ptr
  IRValueNode *res = ir_builder_create_load(builder, i32_type, x_ptr);
  ir_value_set_name(res, "res");
  // ret i32 %res
  ir_builder_create_ret(builder, res);

  ir_builder_destroy(builder);
  return func;
}

// 辅助函数：统计函数中有多少条指定 Opcode 的指令
static int
count_instructions(IRFunction *func, IROpcode opcode)
{
  int count = 0;
  IDList *bb_node;
  list_for_each(&func->basic_blocks, bb_node)
  {
    IRBasicBlock *bb = list_entry(bb_node, IRBasicBlock, list_node);
    IDList *inst_node;
    list_for_each(&bb->instructions, inst_node)
    {
      IRInstruction *inst = list_entry(inst_node, IRInstruction, list_node);
      if (inst->opcode == opcode)
      {
        count++;
      }
    }
  }
  return count;
}

int
main(void)
{
  printf("--- Running test_mem2reg ---\n");

  IRContext *ctx = ir_context_create();
  IRModule *mod = ir_module_create(ctx, "test_mem2reg_module");
  IRFunction *func = build_test_function(mod);

  printf("--- 1. IR Before mem2reg ---\n");
  ir_function_dump(func, stdout);

  // --- 验证 (Pre-Verify) ---
  // 检查初始状态
  // 1 alloca, 3 stores, 1 load
  assert(count_instructions(func, IR_OP_ALLOCA) == 1);
  assert(count_instructions(func, IR_OP_STORE) == 3);
  assert(count_instructions(func, IR_OP_LOAD) == 1);
  assert(count_instructions(func, IR_OP_PHI) == 0);
  printf("Pre-transform checks PASSED.\n");

  // --- 2. 运行分析 Pass 链 ---

  // [!!] 重要: mem2reg 需要一个 *独立的* 竞技场
  // 因为它会分配 BitSet, Stack 等。
  // 我们假设 verifier 有一个 'analysis_arena'
  // 这里我们手动创建一个临时的。
  Bump analysis_arena;
  bump_init(&analysis_arena);

  // 2.1 CFG
  // (cfg_build 使用自己的内部 arena, 所以我们传 &analysis_arena (用于 cfg 结构体自身))
  FunctionCFG *cfg = cfg_build(func, &analysis_arena);
  assert(cfg && "CFG build failed");
  assert(cfg->num_nodes == 5); // entry, cond, then, else, merge

  // 2.2 Dominator Tree
  DominatorTree *dt = dom_tree_build(cfg, &analysis_arena);
  assert(dt && "DomTree build failed");

  // 2.3 Dominance Frontier
  DominanceFrontier *df = ir_analysis_dom_frontier_compute(dt, &analysis_arena);
  assert(df && "DomFrontier build failed");

  printf("\n--- 3. Running mem2reg Pass ---\n");

  // --- 3. 运行 mem2reg ---
  bool changed = ir_transform_mem2reg_run(func, dt, df);

  // --- 4. 验证 (Post-Verify) ---

  // 4.1 检查 Pass 是否报告了修改
  assert(changed == true && "mem2reg reported no changes!");
  printf("Pass reported changes (good).\n");

  // 4.2 检查 IR 结构
  // alloca, load, store 都应该被清除了
  assert(count_instructions(func, IR_OP_ALLOCA) == 0);
  assert(count_instructions(func, IR_OP_STORE) == 0);
  assert(count_instructions(func, IR_OP_LOAD) == 0);
  printf("[DEBUG] count_instructions(func, IR_OP_PHI): %d\n", count_instructions(func, IR_OP_PHI));
  // 应该在 'merge' 块中插入了一个 PHI
  assert(count_instructions(func, IR_OP_PHI) == 1);
  printf("Instruction count checks PASSED.\n");

  // 4.3 检查 PHI 节点
  IDList *bb_node;
  list_for_each(&func->basic_blocks, bb_node)
  {
    IRBasicBlock *bb = list_entry(bb_node, IRBasicBlock, list_node);
    if (strcmp(bb->label_address.name, "merge") == 0)
    {
      // 'merge' 块
      IDList *inst_node = bb->instructions.next;
      IRInstruction *phi = list_entry(inst_node, IRInstruction, list_node);
      assert(phi->opcode == IR_OP_PHI);
      // PHI 应该有两个 *传入对* [val, bb]，
      // 这意味着总共有 4 个操作数
      IDList *head = &phi->operands;

      IRUse *use1 = list_entry(head->next, IRUse, user_node);           // Pair 1 (Value from then)
      IRUse *use2 = list_entry(use1->user_node.next, IRUse, user_node); // Pair 1 (Block 'then')
      assert(use2->user_node.next != head && "PHI should have more than 2 operands");

      IRUse *use3 = list_entry(use2->user_node.next, IRUse, user_node); // Pair 2 (Value from else)
      IRUse *use4 = list_entry(use3->user_node.next, IRUse, user_node); // Pair 2 (Block 'else')

      // 检查 use4 之后是否是链表头
      printf("[DEBUG] use4->user_node.next: %p\n", use4->user_node.next);
      printf("[DEBUG] &phi->operands: %p\n", &phi->operands);
      assert(use4->user_node.next == head && "PHI should have exactly 2 pairs (4 operands)");
      printf("PHI node found in 'merge' block (good).\n");
    }
  }

  // 4.4 运行完整的 IR Verifier
  // 这将检查 SSA 支配规则
  bool verify_ok = ir_verify_function(func);
  assert(verify_ok == true && "IR Verifier failed after mem2reg!");
  printf("IR Verifier PASSED.\n");

  printf("\n--- 5. IR After mem2reg ---\n");
  ir_function_dump(func, stdout);

  // --- 清理 ---
  cfg_destroy(cfg);              // 销毁 CFG (释放它的内部 arena)
  bump_destroy(&analysis_arena); // 销毁我们手动的 arena (释放 dt, df)
  ir_context_destroy(ctx);       // 销毁 IR

  printf("\n--- test_mem2reg PASSED ---\n");
  return 0;
}
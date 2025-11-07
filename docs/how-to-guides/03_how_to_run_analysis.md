# 指南：如何运行分析遍 (Analysis Pass)

`Calico` 不仅是一个 IR，它还包含一套强大的分析工具，用于理解 IR 的结构。这些工具是构建优化（Transforms）和高级校验（Verifier）的基础。

本指南将向你展示如何手动计算一个函数的三个核心分析：

1. **控制流图 (CFG)** - `analysis/cfg.h`
2. **支配树 (Dominator Tree)** - `analysis/dom_tree.h`
3. **支配前沿 (Dominance Frontier)** - `analysis/dom_frontier.h`

## 3.1. 依赖顺序

这些分析遍之间存在严格的依赖关系：

`IRFunction` -> **CFG** -> **Dominator Tree** -> **Dominance Frontier**

你必须按照这个顺序来计算它们，因为后一个 Pass 的计算依赖于前一个 Pass 的结果。

## 3.2. 内存管理：Arena

所有的分析遍（`cfg_build`, `dom_tree_build`, `ir_analysis_dom_frontier_compute`）都需要一个 `Bump *arena` 来进行内存分配。

**最佳实践**是为所有分析遍创建一个**单独的、临时的 Arena**，并在使用完所有分析结果后**一次性销毁**这个 Arena。

## 3.3. 目标：我们要分析什么？

我们将使用 `IRBuilder` 构建一个经典的 "if-then-else" 结构，然后分析它。

**目标 IR：**

```llvm
define i32 @test_analysis(%cond: i1) {
$entry:
  br %cond: i1, $then, $else
$then:
  %x: i32 = 10: i32
  br $end
$else:
  %y: i32 = 20: i32
  br $end
$end:
  %res: i32 = phi [ %x: i32, $then ], [ %y: i32, $else ]
  ret %res: i32
}
```

这个结构有 4 个基本块：`$entry`, `$then`, `$else`, `$end`。

## 3.4. 完整的 C 代码示例

这个示例将演示如何：

1. （快速）构建上述的 IR。
2. 创建一个临时 Arena。
3. **按顺序计算所有分析遍**。
4. **使用分析结果**（例如，检查 `$entry` 是否支配 `$end`）。
5. 清理所有资源。

<!-- end list -->

```c
/* my_analysis_test.c */
#include <stdio.h>
#include <assert.h>

/* 核心 IR 头文件 */
#include "ir/context.h"
#include "ir/module.h"
#include "ir/function.h"
#include "ir/basicblock.h"
#include "ir/builder.h"
#include "ir/constant.h"
#include "ir/type.h"
#include "ir/verifier.h"
#include "utils/id_list.h" // 用于打印

/* 分析头文件 (本指南的主角) */
#include "analysis/cfg.h"
#include "analysis/dom_tree.h"
#include "analysis/dom_frontier.h"

/* 内存管理 */
#include "utils/bump.h"

/**
 * @brief 快速构建我们的“目标 IR”
 * (这是一个简化的构建器，不包含错误检查)
 */
static IRFunction *
build_analysis_function(IRContext *ctx, IRModule *mod)
{
  IRType *i32 = ir_type_get_i32(ctx);
  IRType *i1 = ir_type_get_i1(ctx);
  IRBuilder *b = ir_builder_create(ctx);

  IRFunction *func = ir_function_create(mod, "test_analysis", i32);
  IRArgument *cond_arg = ir_argument_create(func, i1, "cond");
  ir_function_finalize_signature(func, false);

  IRBasicBlock *bb_entry = ir_basic_block_create(func, "entry");
  IRBasicBlock *bb_then = ir_basic_block_create(func, "then");
  IRBasicBlock *bb_else = ir_basic_block_create(func, "else");
  IRBasicBlock *bb_end = ir_basic_block_create(func, "end");

  ir_function_append_basic_block(func, bb_entry);
  ir_function_append_basic_block(func, bb_then);
  ir_function_append_basic_block(func, bb_else);
  ir_function_append_basic_block(func, bb_end);

  // $entry
  ir_builder_set_insertion_point(b, bb_entry);
  ir_builder_create_cond_br(b, &cond_arg->value, &bb_then->label_address, &bb_else->label_address);

  // $then
  ir_builder_set_insertion_point(b, bb_then);
  IRValueNode *val_x = ir_constant_get_i32(ctx, 10);
  ir_builder_create_br(b, &bb_end->label_address);

  // $else
  ir_builder_set_insertion_point(b, bb_else);
  IRValueNode *val_y = ir_constant_get_i32(ctx, 20);
  ir_builder_create_br(b, &bb_end->label_address);

  // $end
  ir_builder_set_insertion_point(b, bb_end);
  IRValueNode *phi = ir_builder_create_phi(b, i32, "res");
  ir_phi_add_incoming(phi, val_x, bb_then);
  ir_phi_add_incoming(phi, val_y, bb_else);
  ir_builder_create_ret(b, phi);

  ir_builder_destroy(b);
  return func;
}

/**
 * @brief 在一个函数上运行并使用所有分析
 */
void
run_analysis_on_function(IRFunction *func, IRContext *ctx)
{
  printf("\n--- 分析函数: @%s ---\n", func->entry_address.name);

  // --- 1. 创建 Arena ---
  // 我们为所有分析创建一个临时的竞技场
  Bump analysis_arena;
  bump_init(&analysis_arena);

  // --- 2. 构建 CFG ---
  printf("Building CFG...\n");
  FunctionCFG *cfg = cfg_build(func, &analysis_arena);
  assert(cfg != NULL && "CFG build failed");
  assert(cfg->num_nodes == 4); // $entry, $then, $else, $end

  // --- 3. 构建支配树 ---
  printf("Building Dominator Tree...\n");
  DominatorTree *dt = dom_tree_build(cfg, &analysis_arena);
  assert(dt != NULL && "DomTree build failed");

  // --- 4. 构建支配前沿 ---
  printf("Building Dominance Frontier...\n");
  DominanceFrontier *df = ir_analysis_dom_frontier_compute(dt, &analysis_arena);
  assert(df != NULL && "DomFrontier build failed");

  // --- 5. 使用分析结果 ---
  printf("Querying analysis results...\n");

  // 获取基本块的引用
  IRBasicBlock *entry = cfg_get_node(cfg, func->basic_blocks.next)->block;
  IRBasicBlock *then = cfg_get_node(cfg, entry->list_node.next)->block;
  IRBasicBlock *else_ = cfg_get_node(cfg, then->list_node.next)->block;
  IRBasicBlock *end = cfg_get_node(cfg, else_->list_node.next)->block;

  // A. 查询支配树 (DomTree)
  // $entry 支配所有块吗？是。
  assert(dom_tree_dominates(dt, entry, entry));
  assert(dom_tree_dominates(dt, entry, then));
  assert(dom_tree_dominates(dt, entry, else_));
  assert(dom_tree_dominates(dt, entry, end));

  // $then 支配 $end 吗？否。
  assert(!dom_tree_dominates(dt, then, end));

  // $end 的立即支配者 (idom) 是谁？是 $entry。
  assert(dom_tree_get_idom(dt, end) == entry);

  printf("[OK] Dominator Tree queries passed.\n");

  // B. 查询支配前沿 (DomFrontier)
  // $then 的支配前沿是什么？应该是 {$end}
  Bitset *df_then = ir_analysis_dom_frontier_get(df, then);
  CFGNode *end_node = cfg_get_node(cfg, end);
  assert(bitset_test(df_then, end_node->id));

  // $else 的支配前沿是什么？也应该是 {$end}
  Bitset *df_else = ir_analysis_dom_frontier_get(df, else_);
  assert(bitset_test(df_else, end_node->id));

  printf("[OK] Dominance Frontier queries passed.\n");

  // --- 6. 清理 ---
  // 注意：我们不需要手动调用 cfg_destroy, dom_tree_destroy 等，
  // 因为它们是“哑”结构体，所有内存都在 arena 中。
  // 我们只需要销毁这个 arena。
  bump_destroy(&analysis_arena);
  printf("Analysis arena destroyed.\n");
}

// -----------------------------------------------------------------
// --- Main 函数：设置、构建、验证、分析 ---
// -----------------------------------------------------------------
int
main()
{
  IRContext *ctx = ir_context_create();
  IRModule *mod = ir_module_create(ctx, "analysis_module");

  // 1. 构建 IR
  IRFunction *func = build_analysis_function(ctx, mod);

  // 2. 验证它
  printf("Verifying function...\n");
  if (!ir_verify_function(func))
  {
    fprintf(stderr, "Function verification FAILED!\n");
    ir_context_destroy(ctx);
    return 1;
  }
  printf("[OK] Function verified.\n");

  // 3. 运行分析
  run_analysis_on_function(func, ctx);

  // 4. 清理
  ir_context_destroy(ctx);
  return 0;
}
```

## 3.5. 编译与运行

这个示例比之前的更复杂，你需要确保所有的头文件都已正确包含。

1. **编译程序**（在 `calico` 根目录运行）：
   
   ```bash
   clang -std=c23 -g -Wall -Iinclude -o build/my_analysis_test my_analysis_test.c -Lbuild -lcalico -lm
   ```

2. **运行程序**：
   
   ```bash
   ./build/my_analysis_test
   ```

3. **预期输出**：
   
   ```
   Verifying function...
   [OK] Function verified.
   
   --- 分析函数: @test_analysis ---
   Building CFG...
   Building Dominator Tree...
   Building Dominance Frontier...
   Querying analysis results...
   [OK] Dominator Tree queries passed.
   [OK] Dominance Frontier queries passed.
   Analysis arena destroyed.
   ```

## 3.6. 下一步

你已经掌握了如何*分析* IR。最后一步是学习如何*转换* IR。

**[-\> 下一篇：指南：如何转换为 SSA (运行 Mem2Reg)](04_how_to_run_mem2reg.md)**

# 指南：如何转换为 SSA (运行 Mem2Reg)

这是“操作指南”系列中最重要的一篇。`Mem2Reg` (Memory to Register) 是一个转换遍 (Transform Pass)，它能将“C 风格”的、依赖 `alloca` (栈分配), `load` (读取) 和 `store` (写入) 的代码，转换为“纯 SSA”形式的、使用 `phi` 节点的 IR。

如果你在为 `Calico` 编写编译器前端，这个 Pass 几乎是**必需的**。它允许你的前端“偷懒”，将所有局部变量都生成为 `alloca`，然后让 `Mem2Reg` 自动为你处理 SSA 的构建。

本指南的核心 API 是 `transforms/mem2reg.h`。

## 4.1. Pass 的依赖

`mem2reg` 算法需要**精确的**控制流和支配信息才能工作。

**运行 `ir_transform_mem2reg_run` 之前，你必须先计算：**

1. **CFG** (`cfg_build`)
2. **Dominator Tree** (`dom_tree_build`)
3. **Dominance Frontier** (`ir_analysis_dom_frontier_compute`)

## 4.2. "Before" vs "After"

我们的目标是将这样的 IR：

**"Before" (使用 Alloca):**

```llvm
define i32 @test_mem2reg(%cond: i1) {
$entry:
  %var: <i32> = alloca i32  ; <-- 1. 在入口处分配
  br %cond: i1, $then, $else
$then:
  store 10: i32, %var: <i32> ; <-- 2. 在 $then 块写入
  br $end
$else:
  store 20: i32, %var: <i32> ; <-- 3. 在 $else 块写入
  br $end
$end:
  %res: i32 = load %var: <i32>  ; <-- 4. 在 $end 块读取
  ret %res: i32
}
```

...**自动转换**为这样高效的 IR：

**"After" (使用 PHI):**

```llvm
define i32 @test_mem2reg(%cond: i1) {
$entry:
  ; alloca 已被移除
  br %cond: i1, $then, $else
$then:
  ; store 已被移除
  br $end
$else:
  ; store 已被移除
  br $end
$end:
  ; load 被 PHI 节点替换
  %res: i32 = phi [ 10: i32, $then ], [ 20: i32, $else ]
  ret %res: i32
}
```

## 4.3. 完整的 C 代码示例

这个示例将演示完整的流程：

1. 构建 "Before" 版本的 IR。
2. 打印它。
3. 运行所有分析遍。
4. 运行 `mem2reg` 转换。
5. 打印 "After" 版本的 IR (已被就地修改)。
6. 再次校验，确保转换后的 IR 仍然有效。

<!-- end list -->

```c
/* my_mem2reg_test.c */
#include <stdio.h>
#include <assert.h>

/* 核心 IR 头文件 */
#include "ir/context.h.h"
#include "ir/module.h"
#include "ir/function.h"
#include "ir/basicblock.h"
#include "ir/builder.h"
#include "ir/constant.h"
#include "ir/type.h"
#include "ir/verifier.h"
#include "ir/printer.h" // 需要打印
#include "utils/id_list.h"

/* 分析头文件 */
#include "analysis/cfg.h"
#include "analysis/dom_tree.h"
#include "analysis/dom_frontier.h"

/* 转换头文件 (本指南的主角) */
#include "transforms/mem2reg.h"

/* 内存管理 */
#include "utils/bump.h"

/**
 * @brief 构建我们的 "Before" IR (使用 alloca/load/store)
 */
static IRFunction *
build_before_function(IRContext *ctx, IRModule *mod)
{
  IRType *i32 = ir_type_get_i32(ctx);
  IRType *i1 = ir_type_get_i1(ctx);
  IRBuilder *b = ir_builder_create(ctx);

  IRFunction *func = ir_function_create(mod, "test_mem2reg", i32);
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

  // --- $entry ---
  ir_builder_set_insertion_point(b, bb_entry);
  // %var: <i32> = alloca i32
  IRValueNode *alloca_var = ir_builder_create_alloca(b, i32, "var");
  // br %cond: i1, $then, $else
  ir_builder_create_cond_br(b, &cond_arg->value, &bb_then->label_address, &bb_else->label_address);

  // --- $then ---
  ir_builder_set_insertion_point(b, bb_then);
  IRValueNode *val_10 = ir_constant_get_i32(ctx, 10);
  // store 10: i32, %var: <i32>
  ir_builder_create_store(b, val_10, alloca_var);
  ir_builder_create_br(b, &bb_end->label_address);

  // --- $else ---
  ir_builder_set_insertion_point(b, bb_else);
  IRValueNode *val_20 = ir_constant_get_i32(ctx, 20);
  // store 20: i32, %var: <i32>
  ir_builder_create_store(b, val_20, alloca_var);
  ir_builder_create_br(b, &bb_end->label_address);

  // --- $end ---
  ir_builder_set_insertion_point(b, bb_end);
  // %res: i32 = load %var: <i32>
  IRValueNode *res = ir_builder_create_load(b, alloca_var, "res");
  // ret %res: i32
  ir_builder_create_ret(b, res);

  ir_builder_destroy(b);
  return func;
}

// -----------------------------------------------------------------
// --- Main 函数：构建、分析、转换、验证 ---
// -----------------------------------------------------------------
int
main()
{
  IRContext *ctx = ir_context_create();
  IRModule *mod = ir_module_create(ctx, "mem2reg_module");

  // 1. 构建 "Before" IR
  IRFunction *func = build_before_function(ctx, mod);

  // 2. 打印 "Before" 状态
  printf("--- [BEFORE Mem2Reg] ---\n");
  ir_module_dump_to_file(mod, stdout);
  printf("-------------------------\n\n");

  // (验证一下我们构建的 IR 是合法的)
  assert(ir_verify_function(func) && "Initial IR failed verification!");

  // 3. 运行分析遍 (mem2reg 的先决条件)
  printf("Running analysis passes (CFG, DomTree, DomFrontier)...\n");
  Bump analysis_arena;
  bump_init(&analysis_arena);

  FunctionCFG *cfg = cfg_build(func, &analysis_arena);
  DominatorTree *dt = dom_tree_build(cfg, &analysis_arena);
  DominanceFrontier *df = ir_analysis_dom_frontier_compute(dt, &analysis_arena);

  // 4. 运行转换！
  printf("Running ir_transform_mem2reg_run()...\n");
  bool transformed = ir_transform_mem2reg_run(func, dt, df);

  if (transformed) {
    printf("[OK] Pass reported successful transformation.\n");
  } else {
    printf("[INFO] Pass reported no transformations occurred.\n");
  }

  // 5. 打印 "After" 状态
  printf("\n--- [AFTER Mem2Reg] ---\n");
  ir_module_dump_to_file(mod, stdout);
  printf("------------------------\n\n");

  // 6. 再次验证！
  // 这是关键一步：确保我们的转换 Pass 没有破坏 IR！
  printf("Verifying transformed IR...\n");
  if (ir_verify_function(func)) {
    printf("[OK] Transformed IR verified successfully.\n");
  } else {
    fprintf(stderr, "[FATAL ERROR] Mem2Reg pass produced invalid IR!\n");
  }

  // 7. 清理
  bump_destroy(&analysis_arena);
  ir_context_destroy(ctx);

  return 0;
}
```

## 4.4. 编译与运行

1. **编译程序**（在 `calico` 根目录运行）：
   
   ```bash
   clang -std=c23 -g -Wall -Iinclude -o build/my_mem2reg_test my_mem2reg_test.c -Lbuild -lcalico -lm
   ```

2. **运行程序**：
   
   ```bash
   ./build/my_mem2reg_test
   ```

3. **预期输出**：
   
   ```
   --- [BEFORE Mem2Reg] ---
   module = "mem2reg_module"
   
   define i32 @test_mem2reg(%cond: i1) {
   $entry:
     %var: <i32> = alloca i32
     br %cond: i1, $then, $else
   $then:
     store 10: i32, %var: <i32>
     br $end
   $else:
     store 20: i32, %var: <i32>
     br $end
   $end:
     %res: i32 = load %var: <i32>
     ret %res: i32
   }
   -------------------------
   
   Running analysis passes (CFG, DomTree, DomFrontier)...
   Running ir_transform_mem2reg_run()...
   [OK] Pass reported successful transformation.
   
   --- [AFTER Mem2Reg] ---
   module = "mem2reg_module"
   
   define i32 @test_mem2reg(%cond: i1) {
   $entry:
     br %cond: i1, $then, $else
   $then:
     br $end
   $else:
     br $end
   $end:
     %0: i32 = phi [ 10: i32, $then ], [ 20: i32, $else ]
     ret %0: i32
   }
   ------------------------
   
   Verifying transformed IR...
   [OK] Transformed IR verified successfully.
   ```

## 4.5. 总结

`mem2reg` Pass 会**就地修改** `IRFunction`：

1. **`find_promotable_allocas`**: 找到所有只被 `load`/`store` 使用的 `alloca`。
2. **`compute_phi_placement`**: 使用支配前沿 (`df`) 决定在哪些块（`$end`）需要插入 `phi` 节点。
3. **`insert_phi_nodes`**: 插入空的 `phi` 节点。
4. **`rename_recursive`**: 遍历支配树 (`dt`)，用一个栈来跟踪 `alloca` 的“当前值”，并替换所有 `load`、`store`，最后填充 `phi` 节点。
5. **清理**: 删除现在已经无用的 `alloca`, `load`, `store` 指令。



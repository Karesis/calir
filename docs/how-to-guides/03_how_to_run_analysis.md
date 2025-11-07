# Guide: How to Run Analysis Passes

`Calico` is more than just an IR; it also includes a powerful suite of analysis tools to understand the IR's structure. These tools are the foundation for building optimizations (Transforms) and advanced diagnostics (like the Verifier).

This guide will show you how to manually compute the three core analyses for a function:

1.  **Control Flow Graph (CFG)** - `analysis/cfg.h`
2.  **Dominator Tree** - `analysis/dom_tree.h`
3.  **Dominance Frontier** - `analysis/dom_frontier.h`

## 3.1. Dependency Order

A strict dependency order exists between these analysis passes:

`IRFunction` -> **CFG** -> **Dominator Tree** -> **Dominance Frontier**

You must compute them in this order, as each pass's calculation depends on the results from the previous one.

## 3.2. Memory Management: The Arena

All analysis passes (`cfg_build`, `dom_tree_build`, `ir_analysis_dom_frontier_compute`) require a `Bump *arena` for their memory allocation.

**Best Practice:** Create a **single, temporary Arena** for all analysis passes and **destroy it once** after you are finished with all the analysis results.

## 3.3. Goal: What Are We Analyzing?

We will use the `IRBuilder` to construct a classic "if-then-else" structure and then analyze it.

**Target IR:**
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
````

This structure has 4 basic blocks: `$entry`, `$then`, `$else`, and `$end`.

## 3.4. Complete C Code Example

This example will demonstrate how to:

1.  (Quickly) build the IR above.
2.  Create a temporary Arena.
3.  **Compute all analysis passes in order.**
4.  **Use the analysis results** (e.g., check if `$entry` dominates `$end`).
5.  Clean up all resources.

<!-- end list -->

```c
/* my_analysis_test.c */
#include <stdio.h>
#include <assert.h>

/* Core IR Headers */
#include "ir/context.h"
#include "ir/module.h"
#include "ir/function.h"
#include "ir/basicblock.h"
#include "ir/builder.h"
#include "ir/constant.h"
#include "ir/type.h"
#include "ir/verifier.h"
#include "utils/id_list.h" // For printing

/* Analysis Headers (The stars of this guide) */
#include "analysis/cfg.h"
#include "analysis/dom_tree.h"
#include "analysis/dom_frontier.h"

/* Memory Management */
#include "utils/bump.h"

/**
 * @brief Quickly builds our "Target IR"
 * (This is a simplified builder, no error checking)
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
 * @brief Runs and uses all analyses on a function
 */
void
run_analysis_on_function(IRFunction *func, IRContext *ctx)
{
  printf("\n--- Analyzing Function: @%s ---\n", func->entry_address.name);

  // --- 1. Create Arena ---
  // We create one temporary arena for all analyses
  Bump analysis_arena;
  bump_init(&analysis_arena);

  // --- 2. Build CFG ---
  printf("Building CFG...\n");
  FunctionCFG *cfg = cfg_build(func, &analysis_arena);
  assert(cfg != NULL && "CFG build failed");
  assert(cfg->num_nodes == 4); // $entry, $then, $else, $end

  // --- 3. Build Dominator Tree ---
  printf("Building Dominator Tree...\n");
  DominatorTree *dt = dom_tree_build(cfg, &analysis_arena);
  assert(dt != NULL && "DomTree build failed");

  // --- 4. Build Dominance Frontier ---
  printf("Building Dominance Frontier...\n");
  DominanceFrontier *df = ir_analysis_dom_frontier_compute(dt, &analysis_arena);
  assert(df != NULL && "DomFrontier build failed");

  // --- 5. Use Analysis Results ---
  printf("Querying analysis results...\n");

  // Get references to the Basic Blocks
  IRBasicBlock *entry = cfg_get_node(cfg, func->basic_blocks.next)->block;
  IRBasicBlock *then = cfg_get_node(cfg, entry->list_node.next)->block;
  IRBasicBlock *else_ = cfg_get_node(cfg, then->list_node.next)->block;
  IRBasicBlock *end = cfg_get_node(cfg, else_->list_node.next)->block;

  // A. Query Dominator Tree
  // Does $entry dominate all blocks? Yes.
  assert(dom_tree_dominates(dt, entry, entry));
  assert(dom_tree_dominates(dt, entry, then));
  assert(dom_tree_dominates(dt, entry, else_));
  assert(dom_tree_dominates(dt, entry, end));

  // Does $then dominate $end? No.
  assert(!dom_tree_dominates(dt, then, end));

  // What is the immediate dominator (idom) of $end? It's $entry.
  assert(dom_tree_get_idom(dt, end) == entry);

  printf("[OK] Dominator Tree queries passed.\n");

  // B. Query Dominance Frontier
  // What is the dominance frontier of $then? It should be {$end}
  Bitset *df_then = ir_analysis_dom_frontier_get(df, then);
  CFGNode *end_node = cfg_get_node(cfg, end);
  assert(bitset_test(df_then, end_node->id));

  // What is the dominance frontier of $else? It should also be {$end}
  Bitset *df_else = ir_analysis_dom_frontier_get(df, else_);
  assert(bitset_test(df_else, end_node->id));

  printf("[OK] Dominance Frontier queries passed.\n");

  // --- 6. Clean up ---
  // Note: We don't need to call cfg_destroy, dom_tree_destroy, etc.
  // They are "dumb" structs; all memory is in the arena.
  // We just need to destroy the one arena.
  bump_destroy(&analysis_arena);
  printf("Analysis arena destroyed.\n");
}

// -----------------------------------------------------------------
// --- Main Function: Setup, Build, Verify, Analyze ---
// -----------------------------------------------------------------
int
main()
{
  IRContext *ctx = ir_context_create();
  IRModule *mod = ir_module_create(ctx, "analysis_module");

  // 1. Build the IR
  IRFunction *func = build_analysis_function(ctx, mod);

  // 2. Verify it
  printf("Verifying function...\n");
  if (!ir_verify_function(func))
  {
    fprintf(stderr, "Function verification FAILED!\n");
    ir_context_destroy(ctx);
    return 1;
  }
  printf("[OK] Function verified.\n");

  // 3. Run analyses
  run_analysis_on_function(func, ctx);

  // 4. Clean up
  ir_context_destroy(ctx);
  return 0;
}
```

## 3.5. Compiling and Running

This example is more complex than previous ones; you need to ensure all headers are included correctly.

1.  **Compile the program** (run from the `calico` root directory):

    ```bash
    clang -std=c23 -g -Wall -Iinclude -o build/my_analysis_test my_analysis_test.c -Lbuild -lcalico -lm
    ```

2.  **Run the program**:

    ```bash
    ./build/my_analysis_test
    ```

3.  **Expected Output**:

    ```
    Verifying function...
    [OK] Function verified.

    --- Analyzing Function: @test_analysis ---
    Building CFG...
    Building Dominator Tree...
    Building Dominance Frontier...
    Querying analysis results...
    [OK] Dominator Tree queries passed.
    [OK] Dominance Frontier queries passed.
    Analysis arena destroyed.
    ```

## 3.6. Next Steps

You've mastered how to *analyze* IR. The final step is to learn how to *transform* it.

**[-\> Next: Guide: How to Convert to SSA (Run Mem2Reg)](04_how_to_run_mem2reg.md)**
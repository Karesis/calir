# Guide: How to Convert to SSA (Run Mem2Reg)

This is the most important guide in the "How-to" series. `Mem2Reg` (Memory to Register) is a transform pass that converts "C-style" code—which relies on `alloca` (stack allocation), `load` (reads), and `store` (writes)—into "pure SSA" form, which uses `phi` nodes.

If you are writing a compiler frontend for `Calico`, this pass is practically **required**. It allows your frontend to be "lazy" by generating all local variables as `alloca`s, and then letting `Mem2Reg` automatically handle the SSA construction for you.

The core API for this guide is `transforms/mem2reg.h`.

## 4.1. Pass Dependencies

The `mem2reg` algorithm requires **precise** control flow and dominance information to work.

**Before running `ir_transform_mem2reg_run`, you must first compute:**

1.  **CFG** (`cfg_build`)
2.  **Dominator Tree** (`dom_tree_build`)
3.  **Dominance Frontier** (`ir_analysis_dom_frontier_compute`)

## 4.2. "Before" vs "After"

Our goal is to transform IR like this:

**"Before" (Using Alloca):**
```llvm
define i32 @test_mem2reg(%cond: i1) {
$entry:
  %var: <i32> = alloca i32  ; <-- 1. Allocate in entry
  br %cond: i1, $then, $else
$then:
  store 10: i32, %var: <i32> ; <-- 2. Write in $then
  br $end
$else:
  store 20: i32, %var: <i32> ; <-- 3. Write in $else
  br $end
$end:
  %res: i32 = load %var: <i32>  ; <-- 4. Read in $end
  ret %res: i32
}
````

...and **automatically transform** it into this efficient IR:

**"After" (Using PHI):**

```llvm
define i32 @test_mem2reg(%cond: i1) {
$entry:
  ; alloca is removed
  br %cond: i1, $then, $else
$then:
  ; store is removed
  br $end
$else:
  ; store is removed
  br $end
$end:
  ; load is replaced by a PHI node
  %res: i32 = phi [ 10: i32, $then ], [ 20: i32, $else ]
  ret %res: i32
}
```

## 4.3. Complete C Code Example

This example will demonstrate the full pipeline:

1.  Build the "Before" version of the IR.
2.  Print it.
3.  Run all analysis passes.
4.  Run the `mem2reg` transform.
5.  Print the "After" version of the IR (which has been modified in-place).
6.  Verify it again to ensure the transformed IR is still valid.

<!-- end list -->

```c
/* my_mem2reg_test.c */
#include <stdio.h>
#include <assert.h>

/* Core IR Headers */
#include "ir/context.h" // Corrected from .h.h
#include "ir/module.h"
#include "ir/function.h"
#include "ir/basicblock.h"
#include "ir/builder.h"
#include "ir/constant.h"
#include "ir/type.h"
#include "ir/verifier.h"
#include "ir/printer.h" // Needed for printing
#include "utils/id_list.h"

/* Analysis Headers */
#include "analysis/cfg.h"
#include "analysis/dom_tree.h"
#include "analysis/dom_frontier.h"

/* Transform Header (The star of this guide) */
#include "transforms/mem2reg.h"

/* Memory Management */
#include "utils/bump.h"

/**
 * @brief Builds our "Before" IR (using alloca/load/store)
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
// --- Main Function: Build, Analyze, Transform, Verify ---
// -----------------------------------------------------------------
int
main()
{
  IRContext *ctx = ir_context_create();
  IRModule *mod = ir_module_create(ctx, "mem2reg_module");

  // 1. Build the "Before" IR
  IRFunction *func = build_before_function(ctx, mod);

  // 2. Print the "Before" state
  printf("--- [BEFORE Mem2Reg] ---\n");
  ir_module_dump_to_file(mod, stdout);
  printf("-------------------------\n\n");

  // (Verify that the IR we built is valid)
  assert(ir_verify_function(func) && "Initial IR failed verification!");

  // 3. Run Analysis Passes (Prerequisites for mem2reg)
  printf("Running analysis passes (CFG, DomTree, DomFrontier)...\n");
  Bump analysis_arena;
  bump_init(&analysis_arena);
  
  FunctionCFG *cfg = cfg_build(func, &analysis_arena);
  DominatorTree *dt = dom_tree_build(cfg, &analysis_arena);
  DominanceFrontier *df = ir_analysis_dom_frontier_compute(dt, &analysis_arena);

  // 4. Run the Transform!
  printf("Running ir_transform_mem2reg_run()...\n");
  bool transformed = ir_transform_mem2reg_run(func, dt, df);
  
  if (transformed) {
    printf("[OK] Pass reported successful transformation.\n");
  } else {
    printf("[INFO] Pass reported no transformations occurred.\n");
  }

  // 5. Print the "After" state
  printf("\n--- [AFTER Mem2Reg] ---\n");
  ir_module_dump_to_file(mod, stdout);
  printf("------------------------\n\n");
  
  // 6. Verify Again!
  // This is the critical step: ensure our transform pass didn't break the IR!
  printf("Verifying transformed IR...\n");
  if (ir_verify_function(func)) {
    printf("[OK] Transformed IR verified successfully.\n");
  } else {
    fprintf(stderr, "[FATAL ERROR] Mem2Reg pass produced invalid IR!\n");
  }

  // 7. Clean up
  bump_destroy(&analysis_arena);
  ir_context_destroy(ctx);
  
  return 0;
}
```

## 4.4. Compiling and Running

1.  **Compile the program** (run from the `calico` root directory):

    ```bash
    clang -std=c23 -g -Wall -Iinclude -o build/my_mem2reg_test my_mem2reg_test.c -Lbuild -lcalico -lm
    ```

2.  **Run the program**:

    ```bash
    ./build/my_mem2reg_test
    ```

3.  **Expected Output**:

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

## 4.5. Summary

The `mem2reg` pass modifies the `IRFunction` **in-place**:

1.  **`find_promotable_allocas`**: Finds all `alloca`s that are only used by `load`s and `store`s.
2.  **`compute_phi_placement`**: Uses the Dominance Frontier (`df`) to decide which blocks (e.g., `$end`) require `phi` nodes.
3.  **`insert_phi_nodes`**: Inserts empty `phi` nodes.
4.  **`rename_recursive`**: Traverses the Dominator Tree (`dt`), using a stack to track the "current value" of the `alloca`, replacing all `load`s and `store`s, and finally filling in the `phi` nodes.
5.  **Cleanup**: Deletes the now-useless `alloca`, `load`, and `store` instructions.
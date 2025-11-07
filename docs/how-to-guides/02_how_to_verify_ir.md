# Guide: How to Verify IR

You've learned how to build IR using the `IRBuilder`, but how do you know the IR you built is "correct"?

`Calico` IR follows a strict set of rules (like SSA). The `Verifier` is your "safety net"—a powerful diagnostic tool used to catch IR errors at runtime.

The core API for this guide is `ir/verifier.h`.

## 2.1. What Does the Verifier Check?

`ir_verify_module` and `ir_verify_function` perform a series of deep checks, including (but not limited to):

* **SSA Dominance Rules**: Ensures that an instruction's operand (if it's an instruction itself) **dominates** that instruction. This is the most central rule of SSA.
* **Terminator Rules**: Ensures every basic block ends with **exactly one** terminator instruction (`ret`, `br`, `cond_br`, `switch`), and that terminators only appear at the end.
* **PHI Node Rules**:
    * Ensures `phi` instructions only appear at the **very beginning** of a basic block.
    * Ensures a `phi` node contains one (and only one) entry for **every** CFG predecessor.
* **Type Consistency**: Ensures all operations (e.g., `add`, `load`, `store`) have correct operand and result types.
* **Use-Def Chains**: Ensures the bi-directional links of the Use-Def chains are intact.
* **Alloca Rules**: Ensures `alloca` instructions only appear in the function's **entry block**.

## 2.2. When Should You Call It?

1.  **After `ir_parse_module`? (Not necessary)**
    * The `ir_parse_module` function (as shown in "Tutorial 2") **already calls `ir_verify_module` internally**. If the parsed IR is invalid, `ir_parse_module` will automatically print an error and return `NULL`. You don't need to do extra work.

2.  **After building with `IRBuilder`? (Strongly Recommended)**
    * **This is the primary use case.** When you manually create instructions with `IRBuilder`, you can inadvertently violate these rules. It's excellent practice to call `ir_verify_module` immediately after you finish building a function or module.

3.  **When writing a Transform Pass? (Required)**
    * Call `ir_verify_function` *before* your pass runs to assert that the IR you received is valid.
    * Call `ir_verify_function` *after* your pass runs to ensure the IR you generated is also valid.

## 2.3. How to Use It

The API is very simple:

* `bool ir_verify_module(IRModule *mod)`
* `bool ir_verify_function(IRFunction *func)`

They return `true` on success. If they fail, they return `false` and **automatically print a detailed, contextual error report to `stderr`**.

### Example: Verifying Our Builder Module

Let's add the verifier to the code from the previous guide (`01_build_with_builder.md`).

```c
/* * Excerpt from 01_build_with_builder.md's main function
 */
#include "ir/verifier.h" // 1. Include the header
#include <stdio.h>
// ... (other includes)

int
main()
{
  // 1. Setup
  IRContext *ctx = ir_context_create();
  IRModule *mod = ir_module_create(ctx, "test_module");

  // 2. Call our build function
  build_readme_ir(mod); // (from the previous guide)

  // 3. Print the result to stdout
  printf("--- Calico IR Dump ---\n");
  ir_module_dump_to_file(mod, stdout);
  printf("--- Dump Complete ---\n");

  // --- 4. Verify! ---
  printf("Verifying module...\n");
  if (ir_verify_module(mod)) {
    printf("[OK] Module verified successfully.\n");
  } else {
    fprintf(stderr, "[ERROR] Module verification FAILED.\n");
  }
  // -------------------

  // 5. Clean up
  ir_context_destroy(ctx);
  return 0;
}
````

Since our `build_readme_ir` function from the previous guide was correct, running this will output:

```
...
--- Dump Complete ---
Verifying module...
[OK] Module verified successfully.
```

## 2.4. Example: A Failed Verification (SSA Violation)

Let's say you try to build IR that violates the SSA dominance rule.

**Example of bad `.cir`:**

```llvm
define i32 @bad_ssa(%cond: i1) {
$entry:
  br %cond: i1, $then, $else
$then:
  %x: i32 = 10: i32
  br $end
$else:
  br $end
$end:
  ; %x is undefined here because it does not dominate the $end block
  %res: i32 = add %x: i32, 1: i32  ; <-- Verifier will fail here
  ret %res: i32
}
```

If you built the IR above using `IRBuilder` and then called `ir_verify_function`, you **would not** get a "Segmentation fault." Instead, you would get a clear error report printed automatically to `stderr`, looking something like this:

```
--- [CALIR VERIFIER ERROR] ---
At:          src/ir/verifier.c:354
In Function: bad_ssa
In Block:    $end
Error:       SSA VIOLATION: Definition in block '$then' does not dominate use in block '$end'.
Object:      %res: i32 = add %x: i32, 1: i32
---------------------------------
[ERROR] Module verification FAILED.
```

This makes debugging your IR generation extremely efficient.

## 2.5. Next Steps

Now you know how to build IR and ensure it's correct. The next step is to learn how to **analyze** these IR structures.

**[-\> Next: Guide: How to Run Analysis Passes](03_how_to_run_analysis.md)**
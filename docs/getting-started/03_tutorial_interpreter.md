# 3. Tutorial: Executing Your IR (Interpreter)

Welcome to the final tutorial in the `Getting Started` series!

In the last chapter, we learned how to parse a `.cir` text string into an in-memory `IRModule`. In this chapter, we'll take it one step further: we will find the `@add` function from that module and **actually execute it** to get a computed result.

The core APIs for this tutorial come from `interpreter/interpreter.h` and `utils/data_layout.h`.

## 3.1. Key API Overview

* **`DataLayout *datalayout_create_host(void)`**
    The interpreter needs to know the type sizes and alignments of the target machine (e.g., `i32` is 4 bytes, `ptr` is 8 bytes). This helper function (from `utils/data_layout.h`) creates a data layout representing the **current running machine** (the "host").

* **`Interpreter *interpreter_create(DataLayout *data_layout)`**
    Creates the interpreter "engine." This is a long-lived object that holds FFI function tables and global variable memory. It will **borrow** the `DataLayout` you pass to it.

* **`RuntimeValue`** (Struct)
    This is the "currency" of the interpreter's world. It's a Tagged Union used to pass data between your C code and the `Calico` IR.
  
  ```c
  RuntimeValue rt_val;
  rt_val.kind = RUNTIME_VAL_I32; // 1. Set the type tag
  rt_val.as.val_i32 = 10;        // 2. Set the corresponding value
  ```

  * **`bool interpreter_run_function(...)`**
    The main API for executing an `IRFunction`.

## 3.2. Complete C Code

We will reuse the code from the previous tutorial and add the interpreter logic on top of it.

Create the file `my_interp_test.c`:

```c
/* my_interp_test.c */

#include <stdio.h>
#include <string.h> // Needed for strcmp to find the function

/* Calico Core Headers */
#include "ir/context.h"
#include "ir/function.h"
#include "ir/module.h"
#include "ir/parser.h"
#include "utils/id_list.h" // Needed for list_for_each

/* New Headers for the Interpreter */
#include "interpreter/interpreter.h"
#include "utils/data_layout.h"

// Our target IR source (same as the previous tutorial)
const char *CIR_SOURCE =
    "module = \"parsed_module\"\n"
    "\n"
    "define i32 @add(%a: i32, %b: i32) {\n"
    "$entry:\n"
    "  %sum: i32 = add %a: i32, %b: i32\n"
    "  ret %sum: i32\n"
    "}\n";

int main() {
  IRContext *ctx = ir_context_create();
  DataLayout *dl = datalayout_create_host(); // 1. Create host data layout
  Interpreter *interp = interpreter_create(dl); // 2. Create interpreter
  IRModule *mod = NULL;

  if (interp == NULL) {
    fprintf(stderr, "Failed to create interpreter.\n");
    goto cleanup;
  }

  // --- 3. Parse (Same as previous tutorial) ---
  mod = ir_parse_module(ctx, CIR_SOURCE);
  if (mod == NULL) {
    fprintf(stderr, "Failed to parse IR.\n");
    goto cleanup;
  }
  printf("IR Parsed successfully.\n");

  // --- 4. Find the @add function ---
  IRFunction *add_func = NULL;
  IDList *it;
  list_for_each(&mod->functions, it) {
    IRFunction *f = list_entry(it, IRFunction, list_node);
    // Note: f->entry_address.name does not include '@'
    if (strcmp(f->entry_address.name, "add") == 0) {
      add_func = f;
      break;
    }
  }

  if (add_func == NULL) {
    fprintf(stderr, "Could not find function '@add' in module.\n");
    goto cleanup;
  }
  printf("Found function '@add'.\n");

  // --- 5. Prepare Input Arguments (10 and 20) ---
  RuntimeValue rt_a;
  rt_a.kind = RUNTIME_VAL_I32;
  rt_a.as.val_i32 = 10;

  RuntimeValue rt_b;
  rt_b.kind = RUNTIME_VAL_I32;
  rt_b.as.val_i32 = 20;

  // The API requires an *array of pointers*
  RuntimeValue *args[] = {&rt_a, &rt_b};

  // --- 6. Execute the Function ---
  RuntimeValue result; // A stack variable to receive the return value
  printf("Executing @add(10, 20)...\n");

  bool success = interpreter_run_function(interp,
                                          add_func, // The function to run
                                          args,     // Array of pointers to arguments
                                          2,        // Number of arguments
                                          &result); // [out] Receives the result

  // --- 7. Print the Result ---
  if (success && result.kind == RUNTIME_VAL_I32) {
    printf("--- Execution Successful ---\n");
    printf("Result: %d\n", result.as.val_i32);
  } else {
    fprintf(stderr, "--- Execution Failed! ---\n");
    // (Note: The interpreter automatically prints detailed errors on failure)
  }

cleanup:
  // 7. Clean up (Order matters)
  // Interpreter is destroyed before DataLayout and Context
  if (interp) interpreter_destroy(interp);
  if (dl) datalayout_destroy(dl);
  if (ctx) ir_context_destroy(ctx); // Context is last (it owns the Module)

  return !success;
}
```

## 3.3. Compiling and Running

Since the `interpreter` code is already compiled into `libcalico.a`, the **compilation command is identical to the previous tutorial**.

1.  **Compile the program** (run from the `calico` root directory):

    ```bash
    clang -std=c23 -g -Wall -Iinclude -o build/my_interp_test my_interp_test.c -Lbuild -lcalico -lm
    ```

2.  **Run the program**:

    ```bash
    ./build/my_interp_test
    ```

3.  **Expected Output**:

    ```
    IR Parsed successfully.
    Found function '@add'.
    Executing @add(10, 20)...
    --- Execution Successful ---
    Result: 30
    ```

## 3.4. API Deep Dive

Let's review the key interpreter API: `interpreter_run_function`.

```c
bool interpreter_run_function(
    Interpreter *interp,     // The interpreter instance
    IRFunction *func,        // The IRFunction you want to run
    RuntimeValue **args,     // [in]  An array of *pointers* to RuntimeValue
    size_t num_args,         //       The number of arguments in the array
    RuntimeValue *result_out // [out] A pointer to a RuntimeValue to write the result into
);
```

  * **`RuntimeValue **args`**: This is the easiest part to get wrong.
    You cannot pass an array of `RuntimeValue`s (`RuntimeValue args[]`).
    You must create the `RuntimeValue`s on the stack, then create an array of **pointers to them** (`RuntimeValue *args[] = {&rt_a, &rt_b};`).

  * **`RuntimeValue *result_out`**:
    This is an "out parameter." You need to declare a `RuntimeValue result;` on your stack, and then pass **its address, `&result`,** to the function. If the function executes successfully and returns (`ret`), the `interpreter` will copy the return value into your `result` variable.

## 3.5. Congratulations\!

You have completed the `Getting Started` series\!

You have learned how to:

1.  **Build**: How to configure `Clang 20+` and use `make test` to verify your environment.
2.  **Parse**: How to use `ir_parse_module` to turn `.cir` text into an `IRModule`.
3.  **Execute**: How to use `interpreter_run_function` to run IR and get results.

-----

## Next Steps

You've mastered how to *use* `Calico`. The next step is to learn how to *create* IR.

**[-\> Next: Guide: Building IR with the `IRBuilder`](../how-to-guides/01_build_with_builder.md)**
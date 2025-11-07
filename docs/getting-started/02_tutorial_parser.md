# 2. Tutorial: Parsing Your First IR

This is the "Hello, World!" tutorial for `Calico`.

Our goal is to write a minimal C program that takes a `calir` IR in **text string** format, uses the `Calico` library to parse it into an **in-memory `IRModule` object**, and verifies its correctness.

The core API for this tutorial is the `ir_parse_module` function provided in `ir/parser.h`.

## 2.1. Target IR Source

First, this is the `.cir` (Calico IR) text we want to parse. It's a simple module that defines an `@add` function, which takes two `i32` parameters and returns their sum.

```llvm
module = "parsed_module"

define i32 @add(%a: i32, %b: i32) {
$entry:
  %sum: i32 = add %a: i32, %b: i32
  ret %sum: i32
}
````

## 2.2. Complete C Code

Please create a new file named `my_parser_test.c` in your `calico` project's root directory and paste the following content:

```c
/* my_parser_test.c */

#include <stdio.h>

/* Include Calico core headers */
#include "ir/context.h" // [!_!] Note: Your include was "ir/context.hh", verify if this is correct.
#include "ir/module.h"
#include "ir/parser.h"  // The star of our tutorial
#include "ir/verifier.h"

// Target IR Source
const char *CIR_SOURCE =
    "module = \"parsed_module\"\n"
    "\n"
    "define i32 @add(%a: i32, %b: i32) {\n"
    "$entry:\n"
    "  %sum: i32 = add %a: i32, %b: i32\n"
    "  ret %sum: i32\n"
    "}\n";

int main() {
  // 1. Create the IR Context
  // This is the "universe" of Calico.
  // It owns all types, constants, and (during parsing) all IR objects.
  IRContext *ctx = ir_context_create();
  
  printf("Parsing IR...\n");

  // 2. Parse the module!
  // This is the core call of this tutorial.
  // It takes the context and a C string, and returns a complete module.
  // If it fails, it returns NULL and automatically prints a detailed
  // error to stderr.
  IRModule *mod = ir_parse_module(ctx, CIR_SOURCE);

  // 3. Handle the result
  if (mod == NULL) {
    // We don't need to print a custom error here,
    // ir_parse_module already did it for us!
    fprintf(stderr, "Test failed.\n");
    ir_context_destroy(ctx);
    return 1;
  }

  printf("--- Parse Successful ---\n");
  printf("Module Name: %s\n", mod->name);

  // 4. (Best Practice) Verify the module
  // Note: ir_parse_module already runs the verifier internally.
  // But ir_verify_module is very useful if you modify the IR manually later.
  if (ir_verify_module(mod)) {
    printf("Module verified successfully.\n");
  } else {
    printf("Module verification failed!\n");
  }

  // 5. Clean up
  // Destroying the context frees all resources it owns (modules, functions, types, etc.)
  ir_context_destroy(ctx);
  return 0;
}
```

*(**注意：** 你的中文版 `include` 示例中有一个错字 `"ir/context.hh"`，我在英文版的注释中标记了它。你的项目 `tree` 显示它应该是 `"ir/context.h"`。请在最终提交时确认一下。)*

## 2.3. Key API Explanations

Let's break down the functions we just used:

  * **`IRContext *ir_context_create(void)`**
    The `IRContext` is the most important struct in `Calico`. It's an "object owner" responsible for managing and interning all unique types, constants, and strings. All other IR objects (like `IRModule`, `IRFunction`) must belong to an `IRContext`.

  * **`IRModule *ir_parse_module(IRContext *ctx, const char *source_buffer)`**
    This is the **main entry point** from `ir/parser.h`.

      * **Input**:
          * `ctx`: The context you created in the previous step.
          * `source_buffer`: A standard C string (`const char *`) containing the `.cir` text you want to parse.
      * **Output**:
          * **Success**: Returns a pointer to the newly created `IRModule` object.
          * **Failure**: Returns `NULL`. **Importantly**, it also automatically prints a beautifully formatted error message to `stderr`, pointing out the **exact line and column number** of the failure.

  * **`bool ir_verify_module(IRModule *mod)`**
    This is a diagnostic tool used to check if an `IRModule` follows all of `calir`'s rules (e.g., SSA rules, type matching, etc.). `ir_parse_module` automatically calls this before returning, but you can also call it again after manually modifying the IR to ensure correctness.

  * **`void ir_context_destroy(IRContext *ctx)`**
    Frees the `IRContext` and all associated memory it owns (including the `IRModule`, `IRFunction`, `IRType`, etc.).

## 2.4. Compiling and Running

Since you already built `libcalico.a` (in the previous guide, `01_build_and_test.md`), we just need to compile our `my_parser_test.c` and link it against your library.

1.  **Compile the program** (run from the `calico` root directory):

    ```bash
    clang -std=c23 -g -Wall -Iinclude -o build/my_parser_test my_parser_test.c -Lbuild -lcalico -lm
    ```

      * `-Iinclude`: Tells the compiler where to find headers like `"ir/parser.h"`.
      * `-Lbuild`: Tells the linker where to find libraries.
      * `-lcalico`: Links the `libcalico.a` library (short for `libcalico.a`).
      * `-lm`: Links the math library (also included in the `Makefile`).

2.  **Run the program**:

    ```bash
    ./build/my_parser_test
    ```

3.  **Expected Output**:

    ```
    Parsing IR...
    --- Parse Successful ---
    Module Name: parsed_module
    Module verified successfully.
    ```

## 2.5. Powerful Error Reporting

The real magic of the `Calico` parser is how it handles errors. Let's intentionally break the `CIR_SOURCE` string.

**Modify** the `%sum: i32 = add ...` line in `my_parser_test.c` by **deleting the type annotation**:

```c
// ...
    "define i32 @add(%a: i32, %b: i32) {\n"
    "$entry:\n"
    "  %sum = add %a: i32, %b: i32\n"  // ERROR! Removed : i32
    "  ret %sum: i32\n"
    "}\n";
// ...
```

Now, **re-compile and run** (using the same commands as the last step). You don't need to add any new error-handling code to your `main` function; the parser does it all for you:

1.  `make build/my_parser_test` (or run the `clang` command directly)
2.  `./build/my_parser_test`

**Error Output**:

```
Parsing IR...

--- Parse Error ---
Error: 5:3: Expected ':', but got '='
  |
5 |   %sum = add %a: i32, %b: i32
  |       ^

Failed to parse IR.
Test failed.
```

Notice how `ir_parse_module` returned `NULL`, causing `main` to print "Test failed.", but more importantly, `ir_parse_module` **itself** printed a detailed, contextual error message pinpointing the exact problem.

## 2.6. Next Steps

You've successfully parsed text IR into in-memory objects\! From here, you have two main options:

  * **[-\> Tutorial: Executing Your IR](03_tutorial_interpreter.md)** (Learn how to use the `interpreter` to run this `@add` function and get `30`)
  * **[-\> Guide: Building IR with the Builder API](../how-to-guides/01_build_with_builder.md)** (Learn how to build IR using C function calls instead of text)
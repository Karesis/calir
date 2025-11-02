# Calico-IR

**A cross-platform Intermediate Representation (IR) framework written in C, inspired by LLVM.**

`Calico-IR` (or `calir`) is a personal project to build a general-purpose compiler backend. It provides the core data structures, transforms, and analysis passes required to define, build, parse, analyze, transform, and verify SSA-form IR.

This project is part of the "Compiler Principles" coursework at UCAS.

## Core Features

`Calico-IR`'s design is logically divided into five layers:

  * **`utils/` (Core Utilities Layer)**

      * `bump.c`: A high-performance **Bump Allocator** for fast allocation and lifetime management of IR objects.
      * `hashmap.c`: A type-safe, high-performance generic hash map.
      * `id_list.h`: An **intrusive linked list** for managing IR objects (instructions, basic blocks, etc.).
      * `string_buf.h` / `temp_vec.h`: Dynamic collections (`StringBuf` and `void*` vector) optimized for the bump allocator.
      * `bitset.c`: A bitset library for dataflow analysis.

  * **`ir/` (IR Core Layer)**

      * `context.h`: A powerful **`IRContext`** that acts as a central manager for:
          * **Type Interning** (for pointers, arrays, and structs).
          * **Constant Interning** (for integers, floats, `undef`, `null`).
          * **String Interning** (for identifiers).
      * `value.h` / `use.h`: A robust **Use-Def chain** implementation.
      * `type.h`: A rich type system supporting primitive types, pointers, arrays, and named/anonymous structs.
      * `builder.h`: A feature-complete **`IRBuilder` API** for programmatically building IR in C, supporting complex instructions like `alloca`, `load`, `store`, `gep`, and `phi`.
      * `lexer.h` / `parser.h`: A complete **Text IR Parser** (`ir_parse_module`) capable of parsing LLVM-style `.calir` text files back into in-memory IR.
      * `error.h`: Provides **detailed, pretty-printed error diagnostics**, precise to line and column, for the parser.
      * `printer.h`: A flexible **IR Printer** (`IRPrinter`) to serialize in-memory IR to files, `stdout`, or strings.
      * `verifier.h`: A critical **IR Verifier** (`ir_verify_module`) to check IR correctness (e.g., SSA dominance rules, type matching).

  * **`analysis/` (Analysis Layer)**

      * `cfg.h`: **Control Flow Graph (CFG)** generation.
      * `dom_tree.h`: **Dominator Tree** calculation, based on the Lengauer-Tarjan algorithm.
      * `dom_frontier.h`: **Dominance Frontier** calculation (a prerequisite for SSA construction).

  * **`transforms/` (Transforms Layer)**

      * `mem2reg.h`: The classic **"Memory to Register"** pass, promoting `alloca`/`load`/`store` to SSA-form `phi` nodes.

  * **`interpreter/` (Interpreter Layer)**

      * `interpreter.h`: (In development) A **tree-walking interpreter** for executing IR.

-----

## Quick Start

`Calico-IR` provides two primary entry points: **building** IR with the `IRBuilder` or **parsing** IR with `ir_parse_module`.

### Example 1: Using the Builder API to Build IR

The following C code demonstrates how to use the `IRBuilder` API to programmatically build a module containing named structs, global variables, `alloca`, and `gep`.

#### Example C Code (`tests/test_readme_example.c`)

```c
/*
 * (This is a verified test case)
 * Target: Build a function that uses GEP to access structs and arrays
 */
#include "ir/builder.h"
#include "ir/context.h"
#include "ir/function.h"
#include "ir/global.h"
#include "ir/module.h"
#include "ir/type.h"
#include "ir/basicblock.h"
#include "ir/constant.h"
#include "ir/argument.h"
#include <stdio.h>

// Build the example IR structure
static void
build_readme_ir(IRModule *mod)
{
  IRContext *ctx = mod->context;

  // 1. Get/Create types
  IRType *i32_type = ir_type_get_i32(ctx);
  IRType *i64_type = ir_type_get_i64(ctx);
  IRType *void_type = ir_type_get_void(ctx);

  // %point = type { i32, i64 }
  IRType *point_members[2] = {i32_type, i64_type};
  IRType *point_type = ir_type_get_named_struct(ctx, "point", point_members, 2);

  // Anonymous array type: [10 x i32]
  IRType *array_type = ir_type_get_array(ctx, i32_type, 10);

  // %data_packet = type { %point, [10 x i32] }
  IRType *packet_members[2] = {point_type, array_type};
  IRType *data_packet_type = ir_type_get_named_struct(ctx, "data_packet", packet_members, 2);

  // 2. Create Global Variable
  // @g_data = ...
  ir_global_variable_create(mod,
                            "g_data",   // Name
                            array_type, // Type
                            NULL);      // Initializer (NULL = zeroinitializer)

  // 3. Create function and entry
  // define void @test_func(%idx: i32)
  IRFunction *func = ir_function_create(mod, "test_func", void_type);
  IRArgument *arg_idx_s = ir_argument_create(func, i32_type, "idx");
  ir_function_finalize_signature(func, false);
  IRValueNode *arg_idx = &arg_idx_s->value;

  IRBasicBlock *entry_bb = ir_basic_block_create(func, "entry");
  ir_function_append_basic_block(func, entry_bb);

  IRBuilder *builder = ir_builder_create(ctx);
  ir_builder_set_insertion_point(builder, entry_bb);

  // 4. Alloca
  // %packet_ptr = alloc ...
  IRValueNode *packet_ptr =
      ir_builder_create_alloca(builder, data_packet_type, "packet_ptr");

  // 5. Create GEP and Store
  IRValueNode *const_0 = ir_constant_get_i32(ctx, 0);
  IRValueNode *const_1 = ir_constant_get_i32(ctx, 1);
  IRValueNode *const_123 = ir_constant_get_i32(ctx, 123);

  // %elem_ptr = gep ...
  IRValueNode *gep_indices[] = {const_0, const_1, arg_idx};
  IRValueNode *elem_ptr =
      ir_builder_create_gep(builder, data_packet_type, packet_ptr, gep_indices, 3,
                            true /* inbounds */, "elem_ptr");
  // store ...
  ir_builder_create_store(builder, const_123, elem_ptr);

  // 6. Terminator
  ir_builder_create_ret(builder, NULL); // ret void
  ir_builder_destroy(builder);
}

// Main function
int
main()
{
  IRContext *ctx = ir_context_create();
  IRModule *mod = ir_module_create(ctx, "test_module");

  // Build the IR
  build_readme_ir(mod);

  // Print the IR to stdout
  printf("--- Calir IR Dump ---\n");
  ir_module_dump_to_file(mod, stdout);
  printf("--- Dump Complete ---\n");

  ir_context_destroy(ctx);
  return 0;
}
```

#### Expected Output (`ir_module_dump_to_file`)

```llvm
--- Calir IR Dump ---
module = "test_module"

%point = type { i32, i64 }
%data_packet = type { %point, [10 x i32] }

@g_data = global [10 x i32] zeroinitializer

define void @test_func(%idx: i32) {
$entry:
  %packet_ptr: <%data_packet> = alloc %data_packet
  %elem_ptr: <i32> = gep inbounds %packet_ptr: <%data_packet>, 0: i32, 1: i32, %idx: i32
  store 123: i32, %elem_ptr: <i32>
  ret void
}
--- Dump Complete ---
```

-----

### Example 2: Parsing and Verifying Text IR

The `ir/parser` module can parse text IR back into memory objects and provides detailed error reporting.

```c
#include "ir/context.h"
#include "ir/module.h"
#include "ir/parser.h"
#include "ir/verifier.h"
#include <stdio.h>

// Our IR source file
const char *IR_SOURCE =
    "module = \"parsed_module\"\n"
    "\n"
    "define i32 @add(%a: i32, %b: i32) {\n"
    "$entry:\n"
    "  %sum: i32 = add %a: i32, %b: i32\n"
    "  ret %sum: i32\n"
    "}\n";

int
main()
{
  IRContext *ctx = ir_context_create();
  
  // 1. Parse
  IRModule *mod = ir_parse_module(ctx, IR_SOURCE);

  if (mod == NULL)
  {
    fprintf(stderr, "Failed to parse IR.\n");
    ir_context_destroy(ctx);
    return 1;
  }
  printf("Parse successful. Module: %s\n", mod->name);

  // 2. Verify (optional, but recommended)
  if (ir_verify_module(mod))
  {
    printf("Module verified successfully.\n");
  }

  ir_context_destroy(ctx);
  return 0;
}
```

#### Parser Error Reporting

If the IR contains a syntax error (e.g., `%sum = add` instead of `%sum: i32 = add`), the parser pinpoints the error:

```
$ ./my_parser_test

--- Parse Error ---
Error: 5:3: Expected ':', but got '='
  |
5 |   %sum = add %a: i32, %b: i32
  |       ^

Failed to parse IR.
```

-----

## Building and Testing

### Dependencies

  * `make`
  * C Compiler (e.g., `gcc` or `clang`)

### Running

(Managed automatically by the `Makefile`)

1.  **Build and run all tests (Recommended):**

    ```bash
    make test
    ```

2.  **Build and run a specific test (e.g., `test_parser`):**

    ```bash
    make run_test_parser
    ```

3.  **Build only a specific test (without running):**

    ```bash
    make build/test_parser
    ```

-----

## Project Status

**This project is functionally complete and feature-rich.** The core IR pipeline (Build, Parse, Verify, Transform) is implemented and tested. The completion of the `mem2reg` pass marks the SSA construction pipeline as ready.

Issues and feedback are welcome\!

## Roadmap

The core framework is complete. Future goals are focused on implementing the interpreter and more optimization passes.

  * [ ] **IR Interpreter (`ir/interpreter`)**
      * **(Current Goal)** Implement a tree-walking interpreter for executing and debugging IR.
  * [x] **IR Text Parser (`ir/parser`)**
      * **Completed.** Capable of parsing `.calir` text files with detailed error reporting.
  * [x] **Dominance Frontier (`analysis/dom_frontier`)**
      * **Completed.** A prerequisite for SSA construction.
  * [x] **Mem2Reg Pass (`transforms/mem2reg`)**
      * **Completed.** Implements promotion of `alloca`/`load`/`store` to `phi` nodes.
  * [ ] **Simple Optimizations (`transforms/*`)**
      * [ ] Constant Folding
      * [ ] Dead Code Elimination (DCE)

## License

This project is licensed under the Apache-2.0 License - see the LICENSE file for details.
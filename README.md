# Calico-IR


**A lightweight, zero-dependency, LLVM-inspired Intermediate Representation (IR) framework written in pure C (C23).**

[![Build Status](https://img.shields.io/github/actions/workflow/status/Karesis/calir/ci.yml?style=flat-square&logo=github)](https://github.com/Karesis/calir/actions) <!-- 替换为你的 GitHub Actions 徽章 -->
![GitHub stars](https://img.shields.io/github/stars/Karesis/calir?style=flat-square&logo=github)
[![License](https://img.shields.io/github/license/Karesis/calir?style=flat-square&color=blue)](LICENSE)
![Language](https://img.shields.io/badge/Language-C23-orange.svg?style=flat-square)

`Calico-IR` (or `calir`) is a personal project to build a general-purpose compiler backend, rigorously developed as part of the "Compiler Principles" coursework at UCAS.

It provides the core data structures, transforms, and analysis passes required to define, build, parse, analyze, transform, and verify SSA-form IR.

## Why Calico-IR?

Tired of the 10+ million lines of C++ in the LLVM framework? Calico-IR is designed as a direct answer for learning, prototyping, and teaching.

* **Lightweight & Understandable:** The entire framework is small, heavily commented, and self-contained. It's designed to be studied, not just used.
* **Pure C (C23) with Zero Dependencies:** No C++, no complex build systems, no external libraries. Just `make` and a C23-compliant compiler.
* **Feature-Complete Core:** Don't let "lightweight" fool you. Calir includes:
    * A robust `IRBuilder` API
    * A full **Text IR Parser** with detailed, line-level error reporting
    * A strict **SSA and Type Verifier**
    * Classic **Dominator Analysis** (Lengauer-Tarjan) and Dominance Frontiers
    * The complete **`mem2reg`** pass for SSA construction

## Quick Start 1: Parsing Text IR (The "Hello, World!")

Calico-IR can parse, verify, and print `.calir` text files, complete with detailed error reporting.

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

int main() {
  IRContext *ctx = ir_context_create();
  
  // 1. Parse
  IRModule *mod = ir_parse_module(ctx, IR_SOURCE);

  if (mod == NULL) {
    fprintf(stderr, "Failed to parse IR.\n");
    ir_context_destroy(ctx);
    return 1;
  }
  printf("Parse successful. Module: %s\n", mod->name);

  // 2. Verify (optional, but recommended)
  if (ir_verify_module(mod)) {
    printf("Module verified successfully.\n");
  }

  ir_context_destroy(ctx);
  return 0;
}
````

### Powerful Error Reporting

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

## Quick Start 2: Building IR with the C API

You can also build complex IR programmatically using the `IRBuilder` API.

### Target IR

This is the `.calir` text we want to build. It uses named structs, `alloca`, and the `gep` (Get Element Pointer) instruction.

```llvm
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
```

### Builder C Code

This C code (found in `tests/test_readme_example.c`) generates the IR above.

\<details\>
\<summary\>\<b\>Click to expand\</b\> the C code for \<code\>test\_readme\_example.c\</code\>\</summary\>

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

\</details\>

-----

## Core Features

  * **IR Core (`ir/`)**: Robust **Use-Def chain** implementation, rich type system (primitives, pointers, arrays, named/anonymous structs), central `IRContext` for **type/constant/string interning**, and a feature-complete `IRBuilder` API (`alloca`, `load`, `store`, `gep`, `phi`, etc.).
  * **Text IR (`ir/`)**: A full **Text IR Parser** (`ir_parse_module`) and **IR Printer** (`IRPrinter`) for serializing IR to files, `stdout`, or strings.
  * **Verifier (`ir/`)**: A critical **IR Verifier** (`ir_verify_module`) that checks for correctness (e.g., SSA dominance rules, type matching).
  * **Analysis (`analysis/`)**: Includes **Control Flow Graph (CFG)** generation, **Dominator Tree** calculation (Lengauer-Tarjan), and **Dominance Frontier** calculation.
  * **Transforms (`transforms/`)**: Implements the classic **"Memory to Register" (`mem2reg`)** pass to promote `alloca`/`load`/`store` to SSA-form `phi` nodes.
  * **Utilities (`utils/`)**: High-performance helpers including a **Bump Allocator**, intrusive linked lists, generic hash maps, and bitsets.

## Building and Testing

### Dependencies

  * `make`
  * C11-compliant Compiler (e.g., `gcc` or `clang`)

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

## Roadmap

The core framework is stable. Future goals are focused on implementing the interpreter and more optimization passes.

  * [ ] **IR Interpreter (`ir/interpreter`)**
      * **(Current Goal)** Implement a tree-walking interpreter for executing and debugging IR.
  * [x] **IR Text Parser (`ir/parser`)**
      * **Completed.**
  * [x] **Dominance Frontier (`analysis/dom_frontier`)**
      * **Completed.**
  * [x] **Mem2Reg Pass (`transforms/mem2reg`)**
      * **Completed.**
  * [ ] **Simple Optimizations (`transforms/*`)**
      * [ ] Constant Folding
      * [ ] Dead Code Elimination (DCE)

## Contributing

Contributions, issues, and feedback are warmly welcome\! Please see [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines.

## License

This project is licensed under the Apache-2.0 License. See the [LICENSE](LICENSE) and [NOTICE](NOTICE) files for details.

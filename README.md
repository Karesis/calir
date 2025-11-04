# Calico

**A lightweight, zero-dependency, LLVM-inspired compiler toolkit written in pure C (C23), featuring a complete IR, analysis passes, and an interpreter.**

[![Build Status](https://img.shields.io/github/actions/workflow/status/Karesis/calir/ci.yml?style=flat-square&logo=github)](https://github.com/Karesis/calir/actions) 
![GitHub stars](https://img.shields.io/github/stars/Karesis/calir?style=flat-square&logo=github)
[![License](https://img.shields.io/github/license/Karesis/calir?style=flat-square&color=blue)](LICENSE)
![Language](https://img.shields.io/badge/Language-C23-orange.svg?style=flat-square)

`Calico` is a personal project to build a general-purpose compiler backend, rigorously developed as part of the "Compiler Principles" coursework at UCAS. It is built around a lightweight, LLVM-inspired Intermediate Representation named **`calir`**.

It provides the core data structures, transforms, analysis passes, and an interpreter required to define, build, parse, analyze, transform, verify, and **execute** SSA-form IR.

## Why Calico?

Tired of the 10+ million lines of C++ in the LLVM framework? Calico is designed as a direct answer for learning, prototyping, and teaching.

* **Lightweight & Understandable:** The entire framework is small, heavily commented, and self-contained. It's designed to be studied, not just used.
* **Pure C (C23) with Zero Dependencies:** No C++, no complex build systems, no external libraries. Just `make` and a C23-compliant compiler.
* **Feature-Complete Core:** Don't let "lightweight" fool you. Calico includes:
    * A robust `IRBuilder` API
    * A full **Text IR Parser** (`.cir` files) with detailed, line-level error reporting
    * A tree-walking **IR Interpreter** for immediate execution
    * A strict **SSA and Type Verifier**
    * Classic **Dominator Analysis** (Lengauer-Tarjan) and Dominance Frontiers
    * The complete **`mem2reg`** pass for SSA construction

## Quick Start 1: Parsing Text IR (The "Hello, World!")

Calico can parse, verify, and print `.cir` text files, complete with detailed error reporting.

```c
#include "ir/context.h"
#include "ir/module.h"
#include "ir/parser.h"
#include "ir/verifier.h"
#include <stdio.h>

// Our IR source (e.g., in a .cir file)
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
  
  // 1. Parse
  IRModule *mod = ir_parse_module(ctx, CIR_SOURCE);

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

This is the `.cir` text we want to build. It uses named structs, `alloca`, and the `gep` (Get Element Pointer) instruction.

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

## Quick Start 3: Executing IR with the Interpreter

Calico includes a simple tree-walking interpreter that can directly execute `calir` IR. This is perfect for testing, debugging, or even using `calir` as a scripting backend.

Here is how you can parse the "Hello, World\!" example and execute the `@add` function:

```c
#include "interpreter/interpreter.h"
#include "ir/context.h"
#include "ir/function.h"
#include "ir/module.h"
#include "ir/parser.h"
#include "utils/data_layout.h"
#include <stdio.h>
#include <string.h>

// (From Quick Start 1)
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
  DataLayout *dl = datalayout_create_host();
  Interpreter *interp = interpreter_create(dl);

  // 1. Parse the module
  IRModule *mod = ir_parse_module(ctx, CIR_SOURCE);
  if (mod == NULL) {
    fprintf(stderr, "Failed to parse IR.\n");
    goto cleanup;
  }

  // 2. Find the "@add" function
  IRFunction *add_func = NULL;
  IDList *it;
  list_for_each(&mod->functions, it) {
    IRFunction *f = list_entry(it, IRFunction, list_node);
    if (strcmp(f->entry_address.name, "add") == 0) {
      add_func = f;
      break;
    }
  }

  if (add_func == NULL) {
    fprintf(stderr, "Could not find function '@add' in module.\n");
    goto cleanup;
  }

  // 3. Prepare arguments: 10 and 20
  RuntimeValue rt_a;
  rt_a.kind = RUNTIME_VAL_I32;
  rt_a.as.val_i32 = 10;

  RuntimeValue rt_b;
  rt_b.kind = RUNTIME_VAL_I32;
  rt_b.as.val_i32 = 20;

  RuntimeValue *args[] = {&rt_a, &rt_b};

  // 4. Run the function
  RuntimeValue result;
  bool success = interpreter_run_function(interp, add_func, args, 2, &result);

  // 5. Print the result
  if (success && result.kind == RUNTIME_VAL_I32) {
    printf("Result of @add(10, 20): %d\n", result.as.val_i32);
  } else {
    fprintf(stderr, "Interpreter run failed!\n");
  }

cleanup:
  interpreter_destroy(interp);
  datalayout_destroy(dl);
  ir_context_destroy(ctx);
  return 0;
}
```

**Output:**

```
$ ./my_interpreter_test
Result of @add(10, 20): 30
```

-----

## Core Features

  * **Interpreter (`interpreter/`)**: A tree-walking interpreter capable of executing `calir` IR, complete with stack and heap management, for debugging and testing.
  * **IR Core (`ir/`)**: Robust **Use-Def chain** implementation, rich type system (primitives, pointers, arrays, named/anonymous structs), central `IRContext` for **type/constant/string interning**, and a feature-complete `IRBuilder` API (`alloca`, `load`, `store`, `gep`, `phi`, etc.).
  * **Text IR (`ir/`)**: A full **Text IR Parser** (`ir_parse_module`) and **IR Printer** (`IRPrinter`) for serializing IR to files (`.cir`), `stdout`, or strings.
  * **Verifier (`ir/`)**: A critical **IR Verifier** (`ir_verify_module`) that checks for correctness (e.g., SSA dominance rules, type matching).
  * **Analysis (`analysis/`)**: Includes **Control Flow Graph (CFG)** generation, **Dominator Tree** calculation (Lengauer-Tarjan), and **Dominance Frontier** calculation.
  * **Transforms (`transforms/`)**: Implements the classic **"Memory to Register" (`mem2reg`)** pass to promote `alloca`/`load`/`store` to SSA-form `phi` nodes.
  * **Utilities (`utils/`)**: High-performance helpers including a **Bump Allocator**, intrusive linked lists, generic hash maps, and bitsets.

## Building and Testing

### Dependencies

  * `make`
  * C23-compliant Compiler (e.g., `gcc` or `clang`)

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

The core IR, passes, and interpreter are stable. Future goals are focused on building a high-quality native backend targeting RISC-V.

  * [x] **IR Interpreter (`interpreter/`)**
      * **(Completed)** A tree-walking interpreter for debugging IR.
  * [x] **IR Text Parser (`ir/parser`)**
      * **(Completed)**
  * [x] **Core Instruction Set (`ir/*`)**
      * **(Completed)**
  * [x] **Dominance Frontier (`analysis/dom_frontier`)**
      * **(Completed)**
  * [x] **Mem2Reg Pass (`transforms/mem2reg`)**
      * **(Completed)**
  * [ ] **Backend: RISC-V (Current Goal)**
      * [ ] **Instruction Selection:** Translate `calir` (IR) to `MachineInstr` (MIR) using virtual registers.
      * [ ] **Register Allocation (Graph Coloring):**
          * [ ] **Liveness Analysis:** Build live intervals for all virtual registers.
          * [ ] **Interference Graph:** Build the register interference graph.
          * [ ] **Coloring/Spilling:** Implement Chaitin-Briggs graph coloring and spill-code generation.
      * [ ] **MC/ELF Emitter:**
          * [ ] **Instruction Encoding:** Implement binary encoding for RISC-V `MachineInstr`s.
          * [ ] **Object File Emitter:** Generate a relocatable ELF (`.o`) file containing `.text`, `.symtab`, `.rela.text`, etc.
  * [ ] **Simple Optimizations (`transforms/*`)**
      * [ ] Constant Folding
      * [ ] Dead Code Elimination (DCE)

## Contributing

Contributions, issues, and feedback are warmly welcome\! Please see [CONTRIBUTING.md](https://www.google.com/search?q=CONTRIBUTING.md) for guidelines.

## License

This project is licensed under the Apache-2.0 License. See the [LICENSE](https://www.google.com/search?q=LICENSE) and [NOTICE](https://www.google.com/search?q=NOTICE) files for details.
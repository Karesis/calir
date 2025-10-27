# Calico-IR

**A cross-platform Intermediate Representation (IR) framework written in C, inspired by LLVM.**

`Calico-IR` (or `calir`) is a personal project to build a general-purpose compiler backend. It provides the core data structures and analysis passes required to define, build, analyze, and verify SSA-form IR.

This project is part of the "Compiler Principles" coursework at UCAS.

## Core Features

`Calico-IR`'s design is logically divided into three layers:

* **`utils/` (Core Utilities Layer)**
    * `bump.c`: A high-performance **Bump Allocator** (`permanent_arena` and `ir_arena`) for fast allocation and lifetime management of IR objects.
    * `hashmap.c`: A type-safe, high-performance generic hash map.
    * `bitset.c`: A bitset library for dataflow analysis.

* **`ir/` (IR Core Layer)**
    * `context.h`: A powerful **`IRContext`** that acts as a central manager for:
        * **Type Interning** (for pointers, arrays, and structs).
        * **Constant Interning** (for integers, floats, and `undef`).
        * **String Interning**.
    * `value.h` / `use.h`: A robust **Use-Def chain** implementation.
    * `type.h`: A rich type system supporting primitive types, pointers, arrays, and named/anonymous structs.
    * `builder.h`: A feature-complete **`IRBuilder` API** for safely constructing IR programmatically in C, supporting complex instructions like `alloca`, `load`, `store`, `gep`, and `phi`.
    * `verifier.h`: A critical **IR Verifier** (`ir_verify_module`, `ir_verify_function`) to check IR correctness (e.g., SSA dominance rules, type matching).
    * **IR Dump**: A built-in debug dump function (`ir_module_dump`) to print the in-memory IR to `stdout` in a human-readable format.

* **`analysis/` (Analysis Layer)**
    * `cfg.c`: **Control Flow Graph (CFG)** generation.
    * `dom_tree.c`: **Dominator Tree** calculation, based on the Lengauer-Tarjan algorithm.

## Quick Start: Building and Dumping IR

The core of `Calico-IR` is the `IRContext` and `IRBuilder`. The following example demonstrates how to build a function containing `alloca`, `struct`, `array`, and `getelementptr` (GEP) instructions, and then dump it to the console.

### Example Code (C Builder API)

```c
/*
 * (This is a simplified example; see test files for complete code)
 * * Target: Build a function that uses GEP to access structs and arrays
 */

#include "ir/builder.h"
#include "ir/context.h"
#include "ir/function.h"
#include "ir/module.h"
#include "ir/type.h"
#include <stdio.h>

// Build the function
static void
build_test_function(IRModule *mod)
{
    IRContext *ctx = mod->context;

    // 1. Get/Create types
    IRType *i32_type = ir_type_get_i32(ctx);
    IRType *i64_type = ir_type_get_i64(ctx);
    IRType *void_type = ir_type_get_void(ctx);

    // %point = type { i32, i64 }
    IRType *point_members[2] = {i32_type, i64_type};
    IRType *point_type = ir_type_get_named_struct(ctx, "point", point_members, 2);

    // %array_type = [10 x i32]
    IRType *array_type = ir_type_get_array(ctx, i32_type, 10);

    // %data_packet = type { %point, [10 x i32] }
    IRType *packet_members[2] = {point_type, array_type};
    IRType *data_packet_type = ir_type_get_named_struct(ctx, "data_packet", packet_members, 2);

    // 2. Create function and entry
    // define void @test_func(i32 %idx)
    IRFunction *func = ir_function_create(mod, "test_func", void_type);
    IRArgument *arg_idx = ir_argument_create(func, i32_type, "idx");

    IRBasicBlock *entry_bb = ir_basic_block_create(func, "entry");
    IRBuilder *builder = ir_builder_create(ctx);
    ir_builder_set_insertion_point(builder, entry_bb);

    // 3. Alloca 
    // %point_ptr = alloca %point
    IRValueNode *point_ptr = ir_builder_create_alloca(builder, point_type);
    // %packet_ptr = alloca %data_packet
    IRValueNode *packet_ptr = ir_builder_create_alloca(builder, data_packet_type);
    
    // 4. Create GEP and Load/Store
    IRValueNode *const_0 = ir_constant_get_i32(ctx, 0);
    IRValueNode *const_1 = ir_constant_get_i32(ctx, 1);
    IRValueNode *const_123 = ir_constant_get_i32(ctx, 123);

    // %1 = getelementptr inbounds %data_packet, ptr %packet_ptr, i32 0, i32 1, i32 %idx
    IRValueNode *gep_indices[] = {const_0, const_1, &arg_idx->value};
    IRValueNode *elem_ptr = ir_builder_create_gep(builder, data_packet_type, packet_ptr, gep_indices, 3,
                                                  true /* inbounds */); 
    // store i32 123, ptr %1
    ir_builder_create_store(builder, const_123, elem_ptr);

    // 5. Terminator
    ir_builder_create_ret(builder, NULL); // ret void
    ir_builder_destroy(builder);
}

// Main function
int main()
{
    IRContext *ctx = ir_context_create();
    IRModule *mod = ir_module_create(ctx, "test_module");

    build_test_function(mod);

    // 4. [!!] Dump the IR for the entire module
    printf("--- Calir IR Dump ---\n");
    ir_module_dump(mod, stdout);
    printf("--- Dump Complete ---\n");

    ir_context_destroy(ctx);
    return 0;
}
````

### Expected Output (`ir_module_dump`)

```llvm
--- Calir IR Dump ---

; ModuleID = 'test_module'

%point = type { i32, i64 }
%array_type = type [10 x i32]
%data_packet = type { %point, %array_type }

define void @test_func(i32 %idx) {
entry:
  %point_ptr = alloca %point
  %packet_ptr = alloca %data_packet
  %1 = getelementptr inbounds %data_packet, ptr %packet_ptr, i32 0, i32 1, i32 %idx
  store i32 123, ptr %1
  ret void
}

--- Dump Complete ---
```

## Building and Testing

### Dependencies

  * `make`
  * C Compiler (e.g., `gcc` or `clang`)

### Running

1.  **Build and run all tests:**

    ```bash
    make test
    ```

2.  **Run a specific test (e.g., `test_verifier`):**

    ```bash
    # Compile
    make build/test_verifier

    # Run
    ./build/test_verifier
    ```

## Project Status

**This project is in the early stages of development.** The core IR data structures (`IRContext`, `IRBuilder`, Use-Def chains), analysis passes (CFG, Dominator Tree), and the `Verifier` are feature-complete and tested.

Issues and feedback are welcome\!

## Roadmap

The next core objectives are to implement an IR interpreter and build the full SSA transformation pipeline:

  - [ ] **IR Interpreter (`ir/interpreter`)**
      * **(High Priority)** Implement a simple tree-walking interpreter for the IR, to be used for execution and debugging (next assignment).
  - [ ] **Dominance Frontier (`analysis/dom_frontier`)**
      * The final analysis component needed for SSA construction.
  - [ ] **Mem2Reg Pass (`transforms/mem2reg`)**
      * Implement the classic "memory to register" promotion pass, lifting `alloca`/`load`/`store` to PHI nodes.
  - [ ] **IR Text Parser (`ir/parser`)**
      * Ability to parse `.ll`-style text files back into in-memory IR.
  - [ ] **Simple Optimizations (`transforms/*`)**
      * e.g., Constant Folding, Dead Code Elimination (DCE), etc.

## License

This project is licensed under the Apache-2.0 License - see the LICENSE file for details.

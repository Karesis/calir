# Guide: Building IR with IRBuilder

In the "Getting Started" guide, we learned how to *parse* and *execute* existing `.cir` text. Now, we will learn how to **programmatically build** IR from scratch using the C API.

This is one of `Calico`'s core features and is essential for writing a compiler frontend (for example, lowering your `nyan` language to `calir`) or for dynamically generating code.

The core APIs for this guide are `ir/builder.h` and `ir/context.h`.

## 3.1. Goal: What Are We Building?

Our goal is to use the C API to completely build the IR module represented by the following `.cir` text.

This module includes:
* Named structs (`%point`, `%data_packet`)
* An array type (`[10 x i32]`)
* A global variable (`@g_data`)
* A function using `alloca`, `gep`, and `store`

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
````

## 3.2. The IRBuilder Workflow

The standard flow for using an `IRBuilder` is as follows:

1.  **Get `IRContext`**: This is the "factory" for all types and constants.
2.  **Create `IRModule`**: This is the container for all global variables and functions.
3.  **Get/Create `IRType`**: Get types like `i32`, `void` from the `IRContext`, and define structs.
4.  **Create `IRFunction` and `IRBasicBlock`**: Define the function signature and create its entry block.
5.  **Create `IRBuilder`**: Instantiate the builder.
6.  **Set Insertion Point**: `ir_builder_set_insertion_point(builder, bb)`. **This is the most critical step.**
7.  **Build Instructions**: Call `ir_builder_create_alloca`, `ir_builder_create_gep`, etc. Instructions are automatically inserted at the end of the `insertion_point`.
8.  **(Optional) Print Module**: Use `ir_module_dump_to_file` to verify the result.

## 3.3. Complete C Code Example

The following code (sourced from `tests/test_readme_example.c`) fully demonstrates the workflow above to generate our "Target IR".

```c
/*
 * Target: Build a function that uses GEP to access structs and arrays
 */
#include "ir/builder.h" // Corrected from .hh
#include "ir/context.h"
#include "ir/function.h"
#include "ir/global.h"
#include "ir/module.h"
#include "ir/type.h"
#include "ir/basicblock.h"
#include "ir/constant.h"
#include "ir/argument.h"
#include <stdio.h> // For printing

/**
 * @brief The core IR building logic
 * @param mod The module to build into
 */
static void
build_readme_ir(IRModule *mod)
{
  IRContext *ctx = mod->context;

  // --- 1. Get/Create Types (from context.h) ---
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

  // --- 2. Create Global Variable (from global.h) ---
  // @g_data = global [10 x i32] zeroinitializer
  ir_global_variable_create(mod,
                            "g_data",   // Name
                            array_type, // Type
                            NULL);      // Initializer (NULL = zeroinitializer)

  // --- 3. Create Function and Entry Block (from function.h / basicblock.h) ---
  // define void @test_func(%idx: i32)
  IRFunction *func = ir_function_create(mod, "test_func", void_type);
  IRArgument *arg_idx_s = ir_argument_create(func, i32_type, "idx");
  ir_function_finalize_signature(func, false); // Lock the function signature
  IRValueNode *arg_idx = &arg_idx_s->value; // Get the ValueNode for the argument

  IRBasicBlock *entry_bb = ir_basic_block_create(func, "entry");
  ir_function_append_basic_block(func, entry_bb);

  // --- 4. Create and Set Up Builder (from builder.h) ---
  IRBuilder *builder = ir_builder_create(ctx);
  ir_builder_set_insertion_point(builder, entry_bb); // Critical!

  // --- 5. Build Alloca (Memory Allocation) ---
  // %packet_ptr: <%data_packet> = alloc %data_packet
  IRValueNode *packet_ptr =
      ir_builder_create_alloca(builder, data_packet_type, "packet_ptr");

  // --- 6. Get Constants (from constant.h) ---
  IRValueNode *const_0 = ir_constant_get_i32(ctx, 0);
  IRValueNode *const_1 = ir_constant_get_i32(ctx, 1);
  IRValueNode *const_123 = ir_constant_get_i32(ctx, 123);

  // --- 7. Build GEP (Pointer Arithmetic) ---
  // %elem_ptr: <i32> = gep inbounds %packet_ptr: <%data_packet>, 0: i32, 1: i32, %idx: i32
  IRValueNode *gep_indices[] = {const_0, const_1, arg_idx};
  IRValueNode *elem_ptr =
      ir_builder_create_gep(builder,
                            data_packet_type, // Source type for GEP
                            packet_ptr,       // Base pointer
                            gep_indices,      // Array of indices
                            3,                // Number of indices
                            true /* inbounds */,
                            "elem_ptr");

  // --- 8. Build Store (Memory Write) ---
  // store 123: i32, %elem_ptr: <i32>
  ir_builder_create_store(builder, const_123, elem_ptr);

  // --- 9. Build Terminator ---
  ir_builder_create_ret(builder, NULL); // ret void

  // --- 10. Clean up ---
  ir_builder_destroy(builder);
}

// -----------------------------------------------------------------
// --- Main Function: Setup, Build, and Print ---
// -----------------------------------------------------------------
int
main()
{
  // 1. Setup
  IRContext *ctx = ir_context_create();
  IRModule *mod = ir_module_create(ctx, "test_module");

  // 2. Call our build function
  build_readme_ir(mod);

  // 3. Print the result to stdout
  printf("--- Calico IR Dump ---\n");
  // ir_module_dump_to_file is a helper in ir/printer.h
  ir_module_dump_to_file(mod, stdout);
  printf("--- Dump Complete ---\n");

  // 4. Clean up
  ir_context_destroy(ctx);
  return 0;
}
```

## 3.4. Key API Explanations

  * **`IRBuilder *ir_builder_create(IRContext *ctx)`**
    Creates a new `IRBuilder`. It only holds a reference to the `IRContext`.

  * **`void ir_builder_set_insertion_point(IRBuilder *builder, IRBasicBlock *bb)`**
    The **most important** function in `IRBuilder`. It tells the builder: "All new instructions you create should now be appended to the end of this basic block, `bb`."

  * **`ir_builder_create_...(IRBuilder *builder, ...)`** (e.g., `_alloca`, `_gep`, `_ret`)
    These are the workhorses of the `IRBuilder`. They are responsible for:

    1.  Creating the instruction object (allocating it in `ctx->ir_arena`).
    2.  Setting up the instruction's operands (the Use-Def chain).
    3.  **Automatically inserting** the instruction at the current `insertion_point`.
    4.  Returning an `IRValueNode *` to the instruction's result (if it has one).

  * **`ir_type_get_...(IRContext *ctx, ...)`** (e.g., `_i32`, `_get_ptr`, `_get_named_struct`)
    These are the type factories. The `IRContext` ensures that types are **unique** (interned). If you ask for `ir_type_get_i32(ctx)` twice, you will get a pointer to the **exact same** `IRType` object.

  * **`ir_constant_get_...(IRContext *ctx, ...)`** (e.g., `_i32`)
    These are the constant factories, similar to type factories. `ir_constant_get_i32(ctx, 0)` will always return a pointer to the same `i32 0` constant value.

## 3.5. How to Compile and Run

This example code already exists as one of the official test cases for the `Calico` project, located at `tests/test_readme_example.c`.

You don't need to compile it manually. You can run it directly from the project root using `make`:

```bash
make run_test_readme_example
```

You will see the `stdout` output that perfectly matches our "Target IR" section.

## 3.6. Next Steps

You've mastered how to programmatically build IR\!

**[-\> Next: Guide: How to Run Analysis Passes](02_how_to_run_analysis.md)**
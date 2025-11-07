# Welcome to the Calico Docs

**A lightweight, zero-dependency, LLVM-inspired compiler toolkit written in pure C (C23), featuring a complete IR, analysis passes, and an interpreter.**

`Calico` is a personal project to build a general-purpose compiler backend, rigorously developed as part of the "Compiler Principles" coursework at UCAS. It is built around a lightweight, LLVM-inspired Intermediate Representation named **`calir`**.

This project provides the core data structures, transforms, analysis passes, and an interpreter required to define, build, parse, analyze, transform, verify, and **execute** SSA-form IR.

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

---

## Quick Start 1: Parsing Text IR (The "Hello, World!")

Calico can parse, verify, and print `.cir` text files, complete with powerful error reporting.

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

Besides parsing text, you can also build complex IR programmatically using the `IRBuilder` API.

### Target IR

This is the `.cir` text we want to build using the C API. It uses named structs, `alloca`, and the `gep` (Get Element Pointer) instruction.

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

The `IRBuilder` API provides a fluent interface for creating types, constants, functions, basic blocks, and instructions, automatically handling the Use-Def chains. This is essential for dynamically generating code or implementing a frontend binding in another language.

We provide a full tutorial showing how to use the C API to generate the target IR above.

**[-\> View the full IRBuilder Guide](how-to-guides/01_build_with_builder.md)**

-----

## Quick Start 3: Executing IR with the Interpreter

Calico includes a simple tree-walking interpreter that can directly execute `calir` IR. This is perfect for testing, debugging, or even using `calir` as a scripting backend.

You can parse the "Hello, World\!" example and then execute the `@add` function directly in memory:

```
$ ./my_interpreter_test
Result of @add(10, 20): 30
```

We provide a full tutorial that shows how to load the IR module, find the function, prepare the runtime arguments, and invoke the interpreter.

**[-\> View the full Interpreter Tutorial](getting-started/03_tutorial_interpreter.md)**

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

Contributions, issues, and feedback are warmly welcome\! Please see [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines.

## License

This project is licensed under the Apache-2.0 License. See the [LICENSE](../LICENSE) and [NOTICE](../NOTICE) files for details.
# 1. Architecture Overview

Welcome to Calico's Core Concepts! In the "Getting Started" and "How-to Guides," we learned how to *use* the APIs to build and execute IR. In this section, we'll dive deeper into the "what" and "why" behind these APIs' design.

Calico's IR architecture is heavily inspired by LLVM. It is not a single massive structure, but rather a graph of multiple **hierarchical objects** with **strong ownership**.

The fastest way to understand Calico's architecture is to break it down into two
main concepts:

1.  **Core Containers (Ownership)**: The "parent-child" relationship of IR objects ("who owns what").
2.  **The Value System**: How `IRValue` acts as the "glue" that connects everything ("what it is").

---

## 🏗️ Core Containers: The Ownership Hierarchy

Calico IR has a strict, tree-like ownership structure. Every object (`Function`, `BasicBlock`, etc.) explicitly knows who its "parent" object is.

This hierarchy is as follows:

```

IRContext (The entire "universe")
└─ IRModule (A "translation unit")
├─ IRGlobalVariable (Global @g\_data)
└─ IRFunction (Function @my\_func)
├─ IRArgument (Argument %arg)
└─ IRBasicBlock (Block $entry)
└─ IRInstruction (Instruction %sum = add)

```


---

### 1. `IRContext` (from `ir/context.h`)

**The `IRContext` is Calico's "universe" or "central manager."**

* **Ultimate Owner**: It is the final owner of all *persistent* objects. It manages the memory Arenas used to quickly allocate all other IR objects (`Module`, `Function`, `Type`, etc.).
* **Interning**: It is the "factory" for all types (`Type`) and constants (`Constant`). When you request an `i32` type, the `IRContext` ensures you get a pointer to the **exact same** `i32` type instance. This makes type and constant comparison extremely fast (just a pointer comparison).
* **Lifecycle**: The `IRContext` is the first object you create and the last object you destroy. Destroying the `IRContext` frees *all* IR it owns.

### 2. `IRModule` (from `ir/module.h`)

**The `IRModule` is the "root node" of the IR structure, representing a single translation unit (e.g., one `.c` file).**

* It holds a pointer to its parent `IRContext`.
* It owns a list of `IRGlobalVariable`s (e.g., `@g_data`).
* It owns a list of `IRFunction`s (e.g., `@main`).

### 3. `IRFunction` (from `ir/function.h`)

**An `IRFunction` represents a callable unit of code.**

* It holds a pointer to its parent `IRModule`.
* It defines its **return type** (e.g., `i32`).
* It owns a list of `IRArgument`s (e.g., `%a: i32`, `%b: i32`).
* It owns a list of `IRBasicBlock`s (e.g., `$entry`, `$then`, `$end`).

### 4. `IRBasicBlock` (from `ir/basicblock.h`)

**An `IRBasicBlock` represents a "basic block," a linear sequence of instructions with no internal jumps.**

* It holds a pointer to its parent `IRFunction`.
* It owns a list of `IRInstruction`s.
* A `Calico` `IRBasicBlock` *must* end with a **terminator instruction** (`ret`, `br`, `cond_br`, `switch`).

### 5. `IRInstruction` (from `ir/instruction.h`)

**An `IRInstruction` is the smallest unit of executable code.**

* It holds a pointer to its parent `IRBasicBlock`.
* It has an opcode (`IROpcode`), such as `IR_OP_ADD`.
* It owns a list of **Operands**, which are connected via `IRUse` objects.

---

## 🧬 `IRValue`: The Unified Value System

While the "container" hierarchy above (`Module` -> `Function` -> `Block`) is easy to understand, the real power of `Calico` (and LLVM) comes from its **Value system**.

**`IRValue` (defined in `ir/value.h`) is the most important core concept in `Calico`.**

> **Core Philosophy:** "If a thing can be used as an operand to an instruction, then it is an `IRValue`."

`IRValue` itself is a "base class" (implemented via C struct composition). All of the following objects **are** `IRValue`s:

* **`IRInstruction`** (its *result*)
    * `_**%sum**: i32_ = add %a: i32, %b: i32`
    * `%sum` is an `IRValue`.
* **`IRConstant`**
    * `%sum: i32 = add %a: i32, _**10**: i32_`
    * `10: i32` is an `IRValue`.
* **`IRArgument`**
    * `%sum: i32 = add _**%a**: i32_, %b: i32`
    * `%a` is an `IRValue`.
* **`IRGlobalVariable`**
    * `%ptr: <i32> = bitcast _**@g_data**: <[10xi32]>_ to <i32>`
    * `@g_data` is an `IRValue` (its type is a pointer).
* **`IRBasicBlock`** (its *label*)
    * `br _**$end**_`
    * `$end` is an `IRValue` (its type is `label`).

**What's the benefit?**
This means an `add` instruction **doesn't need to care** if it's adding two `IRArgument`s, two `IRConstant`s, or the results of two other `IRInstruction`s. It only knows that it requires two pointers to `IRValue`s, and that both of those `IRValue`s have the type `i32`.

---

## 🔗 Use-Def Chains (from `ir/use.h`)

The `IRValue` system is connected via **Use-Def chains**.

* **Def-Use (Definition -> Uses)**:
    * An `IRInstruction` (like `add`) owns a list of the `IRValue`s it "uses" (its operand list).
    * Example: The `add` instruction **uses** `%a` and `%b`.
* **Use-Def (Use -> Definition)**:
    * Every `IRValue` (like `%a`) owns a **list** of all instructions that "use" it.
    * Example: The `IRValue` `%a` **is used by** the `add` instruction.

The `IRUse` struct from `ir/use.h` is the "middle-man" that implements this bi-directional link. This makes advanced refactoring like `ir_value_replace_all_uses_with()` (used in `mem2reg`) possible.

## Next Steps

Now that you understand the **data structure** of `Calico` IR, the next step is to learn its **text syntax**.

[-> Next: Core Concepts: The `calir` Text IR Format](02_calir_text_format.md)

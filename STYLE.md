# Calico-IR (calir) Code Style Guide

This document outlines the code style and conventions for the `calir` project. All contributors are expected to follow these rules to ensure the codebase is consistent, readable, and maintainable.

## 1. Automated Tooling (The Hard Rules)

We use a set of automated scripts to enforce most of our style. Before submitting a Pull Request, please ensure you have run these commands locally.

### 1.1. Code Formatting (Clang-Format)

* **Tool:** `clang-format`
* **Config:** The `.clang-format` file in the project root.
* **Action:** Before committing, you **must** run:
    ```bash
    make format
    ```
* **CI Check:** The CI will run `make check-format`. If your code is not formatted, the build will fail.

> **Note on `Language: Cpp`:** The `.clang-format` file uses `Language: Cpp`. This is intentional as it enables richer formatting rules for C (the `Language: C` preset is older and less comprehensive). This project remains a **pure C23 project**.

### 1.2. License Headers

* **Rule:** All `.c` and `.h` source files must include the Apache 2.0 license header.
* **Action:** `make headers` (This will add any missing headers).
* **CI Check:** `make check-headers`

### 1.3. Comment Style

We have a strict convention for comment types to distinguish documentation from temporary notes.

* **`/* ... */`**: (Doxygen style) Used for documenting functions, structs, enums, and public APIs in `include/`.
* **`/// ...`**: Used for **permanent**, important single-line explanations inside function bodies.
* **`// ...`**: Used for **temporary** development, debugging, or `TODO` notes.
* **Action:** Temporary `//` comments are periodically cleaned. You can run the script yourself via:
    ```bash
    make clean-comments
    ```

## 2. Coding Conventions (The Soft Rules)

These are conventions that `clang-format` cannot enforce and must be followed manually.

### 2.1. Language

* **Standard:** This project uses **C23** (`-std=c23`).
* **C++:** Do **not** use C++ features.

### 2.2. Naming Conventions

Our naming style is inspired by Rust and aims for a clean, object-oriented feel within C.

* **Types (struct, enum, union):** `PascalCase`.
    We use a `typedef` to define the type name directly, matching the struct tag.
    ```c
    /**
     * @brief A brief description of the type.
     */
    typedef struct MyType
    {
      int member_one;
      const char *member_two;
    } MyType;
    ```

* **Functions & Methods:** `snake_case`.
    Functions that operate on a "class" (struct) should be prefixed with the module or type name, e.g., `ir_lexer_init(...)` or `lexer_next(...)`.

* **Variables & Parameters:** `snake_case`.
    When a type is used as a parameter, the name should be a `snake_case` version of the type.
    ```c
    void ir_lexer_init(Lexer *lexer, const char *buffer, IRContext *ctx);
    ```

* **Enum Members:** `UPPER_SNAKE_CASE` (e.g., `IR_VALUE_INST`).

* **Macros:** `UPPER_SNAKE_CASE` (e.g., `DEFINE_LIST_IMPL`).

* **API Prefixes:** Public functions and types declared in `include/` **should ideally** be prefixed with the module name (e.g., `ir_...`, `Lexer`) to provide namespacing. This is a guideline, not a strict rule, but helps avoid name collisions.

### 2.3. Pointers

* The pointer asterisk (`*`) should align to the **right** (variable name), not the left (type).
    * **Correct:** `MyType *my_type;`
    * **Incorrect:** `MyType* my_type;`
    * (`clang-format` will enforce this automatically via `PointerAlignment: Right`).

### 2.4. `const`

* Use `const` aggressively.
* If a function does not modify a parameter passed by pointer, mark it `const` (e.g., `const IRModule *mod`).
* If a pointer itself should not be re-assigned, mark it `const` (e.g., `IRModule * const mod`).

### 2.5. Header Files

* Use `#pragma once` for include guards.
* Include order:
    1.  The related header (e.g., `module.c` should include `"ir/module.h"` first).
    2.  Other project headers (e.g., `"utils/hashmap.h"`).
    3.  System/Standard library headers (e.g., `<stdio.h>`, `<stdbool.h>`).
# 2. The `calir` Text IR Format (`.cir`)

The `calir` text format is the "serialized" or "human-readable" version of `Calico`'s in-memory IR objects. Its syntax is heavily inspired by LLVM IR, but with one core design philosophy we call **"Type-Following"**.

## 2.1. Core Philosophy: "Type-Following"

In the `.cir` format, **nearly every "Value Operand" explicitly states its type right at the point of use.**

This design was originally intended to make the parser (`ir/parser.h`) extremely simple and fast, as it doesn't need to perform complex type inference—the type is provided *following* the value.

Observe the following examples:

```llvm
; Variable (Local Identifier)
%sum: i32 = add %a: i32, %b: i32

; Constant
%ten: i32 = 10: i32
store %ten: i32, %ptr: <i32>

; Global Variable (Global Identifier)
%val: i32 = load @g_data: <i32>

; Basic Block (Label Identifier)
br $end
````

  * **Variables/Constants**: Use `%name: type` or `value: type` syntax.
  * **Global Variables**: Use `@name: type` syntax (note the type is a pointer).
  * **Basic Blocks**: Use `$name` syntax (its type is a special `label` type).

The **benefits** of this design are:

1.  **Simple Parsing**: The parser is "context-free." When it sees an `add` instruction, it just needs to parse `[operand]: [type]`, `[operand]: [type]`. It doesn't need to look backward to find `%a`'s definition to infer its type.
2.  **High Readability**: For a human reader, every line of code is self-contained. You don't need to scroll up and down the file to find the types of `%a` or `%b`.
3.  **Precise Error Reporting**: If the type of `%a` doesn't match what the `add` instruction expects, the parser can report an error immediately at the **use-site**.

-----

## 2.2. Type System Syntax

`calir` supports a rich type system, represented in text as follows:

| Type | Text Syntax | Description |
| :--- | :--- | :--- |
| **Primitive Types** | `void` | The void type, for `ret` or non-returning functions |
| | `i1`, `i8`, `i16`, `i32`, `i64` | Integer types of different bit widths |
| | `f32`, `f64` | 32-bit and 64-bit floating-point types |
| **Pointer Type** | `<i32>` | A pointer to `i32` (wrapped in `<` and `>`) |
| | `<[10 x i8]>` | A pointer to an "array of 10 `i8`s" |
| **Array Type** | `[10 x i32]` | An array of 10 `i32` elements |
| **Struct Type** | `{ i32, <i8>, [4 x f32] }` | An anonymous struct |
| | `%my_struct` | A reference to a *defined* named struct |
| **Function Type** | `i32 (i32, i32)` | A function that takes (i32, i32) and returns i32 |
| | `<i32 (i32, i32)>` | A **pointer** to the function type above |

-----

## 2.3. Top-Level Elements

A `.cir` file consists of a series of top-level elements:

### 1\. Module Identifier

(Optional) Defines the name of the module. Must be at the top of the file.

```llvm
module = "my_test_module.c"
```

### 2\. Named Struct Definition

Defines a global, named struct using the `%name = type { ... }` syntax.

```llvm
%point = type { i32, i64 }
%data_packet = type { %point, [10 x i32] }
```

### 3\. Global Variable

Defines a global variable using the `@name = global ...` syntax.

```llvm
@g_data = global [10 x i32] zeroinitializer
@g_flag = global i32 123: i32
```

### 4\. Function Declaration

Declares an external function (e.g., a C library function) without a body.

```llvm
declare i32 @printf(<i8>, ...)
```

### 5\. Function Definition

Defines a function complete with basic blocks and instructions.

```llvm
define i32 @add(%a: i32, %b: i32) {
  ; ... function body ...
}
```

-----

## 2.4. Function Body: Basic Blocks & Instructions

A function body is composed of one or more **Basic Blocks**.

  * **Basic Blocks (Labels)**:
    Each basic block must start with a **label identifier** (prefixed with `$`) and end with a **colon**.

    ```llvm
    $entry:
      ; ... instructions ...
    $then:
      ; ... instructions ...
    ```

  * **Instructions**:
    Instructions are the core of `calir`. They follow two formats:

    1.  **With a return value**: `%result_name: type = opcode [operands]`
    2.  **With no return value**: `opcode [operands]`

### Instruction Examples

**1. Terminator Instructions**
*Every basic block must end with one of these.*

```llvm
ret void
ret %res: i32

br $end

br %cond: i1, $then, $else

switch %val: i32, default $fallback [
  1: i32, $case1,
  2: i32, $case2
]
```

**2. Memory Operations**

```llvm
%ptr: <i32> = alloca i32
store 10: i32, %ptr: <i32>
%val: i32 = load %ptr: <i32>
```

**3. GEP (Get Element Pointer)**
*Used for pointer arithmetic.*

```llvm
; Access the %idx-th element of the 2nd member (array) of %data_packet
%elem_ptr: <i32> = gep inbounds %ptr: <%data_packet>, 0: i32, 1: i32, %idx: i32
```

**4. Arithmetic / Logic Operations**
*Note that all operands follow the "type-following" rule.*

```llvm
%sum: i32 = add %a: i32, %b: i32
%cmp: i1 = icmp slt %a: i32, %b: i32
%res: i64 = zext %cmp: i1 to i64
```

**5. PHI Nodes**
*PHI nodes must be located at the *very top* of a basic block.*

```llvm
$end:
  %res: i32 = phi [ %x: i32, $then ], [ %y: i32, $else ]
  ret %res: i32
```

## 2.5. Next Steps

You now understand `Calico` IR's **in-memory architecture** (previous) and its **text format** (current). You have mastered all the core concepts\!

The next step is to dive into the "dictionary" of the API.

**[-\> Next: API Reference (Coming Soon)](../api-reference/index.md)**
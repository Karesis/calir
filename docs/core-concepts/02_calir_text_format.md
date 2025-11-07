# 2. `calir` 文本 IR 格式 (`.cir`)

`calir` 文本格式是 `Calico` 内存中 IR 对象的“序列化”或“人类可读”的版本。它的语法受到了 LLVM IR 的高度启发，但有一个核心的设计理念，称之为 **“类型跟随” (Type-Following)**。

## 2.1. 核心理念：“类型跟随”

在 `.cir` 格式中，**几乎每一个“值”操作数（Value Operand）都会在原地显式地注明其类型。**

这个设计最初是为了让解析器（`ir/parser.h`）变得极其简单和快速，因为它不需要执行复杂的类型推断——类型是“跟随”值一起提供的。

观察以下例子：

```llvm
; 变量 (局部标识符)
%sum: i32 = add %a: i32, %b: i32

; 常量
%ten: i32 = 10: i32
store %ten: i32, %ptr: <i32>

; 全局变量 (全局标识符)
%val: i32 = load @g_data: <i32>

; 基本块 (标签标识符)
br $end
```

* **变量/常量**: 使用 `%name: type` 或 `value: type` 的语法。
* **全局变量**: 使用 `@name: type` 的语法（注意类型是指针类型）。
* **基本块**: 使用 `$name` 的语法（其类型是特殊的 `label` 类型）。

这种设计的**好处**是：

1. **解析简单 (Simple Parsing)**：解析器是“上下文无关”的。当它看到 `add` 指令时，它只需要解析 `[operand]: [type]`, `[operand]: [type]`。它不需要回溯查找 `%a` 的定义来推断其类型。
2. **可读性高 (Readability)**：对于人类读者来说，每一行指令都是自包含的。你不需要在文件中上下滚动来查找 `%a` 或 `%b` 的类型。
3. **错误报告精确 (Error Reporting)**：如果 `%a` 的类型与 `add` 指令期望的类型不匹配，解析器可以立即在**使用点**（use-site）报告错误。

-----

## 2.2. 类型系统语法 (Type Syntax)

`calir` 支持一个丰富的类型系统，其文本表示如下：

| 类型       | 文本语法                            | 描述                             |
| -------- | ------------------------------- | ------------------------------ |
| **原生类型** | `void`                          | 空类型，用于 `ret` 或不返回的函数           |
|          | `i1`, `i8`, `i16`, `i32`, `i64` | 不同位宽的整数类型                      |
|          | `f32`, `f64`                    | 32位和64位浮点类型                    |
| **指针类型** | `<i32>`                         | 指向 `i32` 的指针 (使用 `<` 和 `>` 包裹) |
|          | `<[10 x i8]>`                   | 指向“10个 `i8` 数组”的指针             |
| **数组类型** | `[10 x i32]`                    | 10个 `i32` 元素的数组                |
| **结构体**  | `{ i32, <i8>, [4 x f32] }`      | 匿名结构体                          |
|          | `%my_struct`                    | 对*已定义*的命名结构体的引用                |
| **函数类型** | `i32 (i32, i32)`                | 一个接收 (i32, i32) 并返回 i32 的函数    |
|          | `<i32 (i32, i32)>`              | 指向上述函数类型的**指针**                |

-----

## 2.3. 顶层元素 (Top-Level Elements)

一个 `.cir` 文件由一系列顶层元素组成：

### 1\. 模块标识符

（可选）定义模块的名称，必须位于文件顶部。

```llvm
module = "my_test_module.c"
```

### 2\. 命名结构体定义

使用 `%name = type { ... }` 语法定义一个全局的、命名的结构体。

```llvm
%point = type { i32, i64 }
%data_packet = type { %point, [10 x i32] }
```

### 3\. 全局变量

使用 `@name = global ...` 语法定义一个全局变量。

```llvm
@g_data = global [10 x i32] zeroinitializer
@g_flag = global i32 123: i32
```

### 4\. 函数声明 (Declare)

声明一个外部函数（例如 C 库函数），没有函数体。

```llvm
declare i32 @printf(<i8>, ...)
```

### 5\. 函数定义 (Define)

定义一个包含基本块和指令的函数。

```llvm
define i32 @add(%a: i32, %b: i32) {
  ; ... 函数体 ...
}
```

-----

## 2.4. 函数体：基本块与指令

函数体由一个或多个**基本块 (Basic Block)** 组成。

* **基本块 (Labels)**:
  每个基本块必须以一个**标签标识符**（以 `$` 开头）和**冒号**结尾。
  
  ```llvm
  $entry:
    ; ... 指令 ...
  $then:
    ; ... 指令 ...
  ```

* **指令 (Instructions)**:
  指令是 `calir` 的核心。它们遵循两种格式：
  
  1. **有返回值**: `%result_name: type = opcode [operands]`
  2. **无返回值**: `opcode [operands]`

### 指令示例

**1. 终结者指令 (Terminators)**
*每个基本块必须以此类指令结束。*

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

**2. 内存操作**

```llvm
%ptr: <i32> = alloca i32
store 10: i32, %ptr: <i32>
%val: i32 = load %ptr: <i32>
```

**3. GEP (Get Element Pointer)**
*用于计算指针偏移。*

```llvm
; 访问 %data_packet 的第二个成员 (数组) 的第 %idx 个元素
%elem_ptr: <i32> = gep inbounds %ptr: <%data_packet>, 0: i32, 1: i32, %idx: i32
```

**4. 算术/逻辑运算**
*注意所有操作数都遵循“类型跟随”规则。*

```llvm
%sum: i32 = add %a: i32, %b: i32
%cmp: i1 = icmp slt %a: i32, %b: i32
%res: i64 = zext %cmp: i1 to i64
```

**5. PHI 节点**
*PHI 节点必须位于基本块的*最顶部\*。\*

```llvm
$end:
  %res: i32 = phi [ %x: i32, $then ], [ %y: i32, $else ]
  ret %res: i32
```

## 2.5. 下一步

你现在已经理解了 `Calico` IR 的**内存中架构**（上一篇）和**文本格式**（本篇）。你已经掌握了所有核心概念！

下一步是深入研究 API 的“字典”。

**[-\> 下一篇：API 参考（敬请期待）](../api-reference/index.md)**

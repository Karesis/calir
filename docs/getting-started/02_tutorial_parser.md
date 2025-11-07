# 2. 教程：解析你的第一个 IR

这是 `Calico` 的 "Hello, World!" 教程。

我们的目标是编写一个最小的 C 程序，它接受一个**文本字符串**形式的 `calir` IR，使用 `Calico` 库将其解析为一个**内存中的 `IRModule` 对象**，并验证其正确性。

本教程的核心 API 是 `ir/parser.h` 中提供的 `ir_parse_module` 函数。

## 2.1. 目标 IR 源码

首先，这是我们想要解析的 `.cir`（Calico IR）文本。这是一个简单的模块，定义了一个 `@add` 函数，它接收两个 `i32` 参数并返回它们的和。

```llvm
module = "parsed_module"

define i32 @add(%a: i32, %b: i32) {
$entry:
  %sum: i32 = add %a: i32, %b: i32
  ret %sum: i32
}
```

## 2.2. 完整的 C 代码

请在你的 `calico` 项目根目录中创建一个名为 `my_parser_test.c` 的新文件，并粘贴以下内容：

```c
/* my_parser_test.c */

#include <stdio.h>

/* 包含 Calico 核心头文件 */
#include "ir/context.hh"
#include "ir/module.h"
#include "ir/parser.h"  // 我们本教程的主角
#include "ir/verifier.h"

// 目标 IR 源码
const char *CIR_SOURCE =
    "module = \"parsed_module\"\n"
    "\n"
    "define i32 @add(%a: i32, %b: i32) {\n"
    "$entry:\n"
    "  %sum: i32 = add %a: i32, %b: i32\n"
    "  ret %sum: i32\n"
    "}\n";

int main() {
  // 1. 创建 IR 上下文 (Context)
  // 这是 Calico 的 "宇宙"
  // 它拥有所有的类型、常量和（在解析时）所有 IR 对象
  IRContext *ctx = ir_context_create();

  printf("正在解析 IR...\n");

  // 2. 解析模块！
  // 这是本教程的核心调用。
  // 它接收上下文和 C 字符串，返回一个完整的模块。
  // 如果失败，它会返回 NULL，并自动将详细错误打印到 stderr。
  IRModule *mod = ir_parse_module(ctx, CIR_SOURCE);

  // 3. 处理结果
  if (mod == NULL) {
    // 我们不需要在这里打印自定义错误，
    // 因为 ir_parse_module 已经自动完成了！
    fprintf(stderr, "测试失败。\n");
    ir_context_destroy(ctx);
    return 1;
  }

  printf("--- 解析成功 ---\n");
  printf("模块名: %s\n", mod->name);

  // 4. (最佳实践) 验证模块
  // 注意：ir_parse_module 内部已经自动运行了一次验证器。
  // 但如果你之后手动修改了 IR，ir_verify_module 会非常有用。
  if (ir_verify_module(mod)) {
    printf("模块已验证通过。\n");
  } else {
    printf("模块验证失败！\n");
  }

  // 5. 清理
  // 销毁上下文会释放它拥有的所有资源 (模块、函数、类型等)
  ir_context_destroy(ctx);
  return 0;
}
```

## 2.3. 关键 API 讲解

让我们分解一下刚才用到的函数：

* **`IRContext *ir_context_create(void)`**
  `IRContext` 是 `Calico` 中最重要的结构体。它是一个“对象池”，负责管理和池化（interning）所有唯一的类型、常量和字符串。所有其他 IR 对象（如 `IRModule`, `IRFunction`）都必须归属于一个 `IRContext`。

* **`IRModule *ir_parse_module(IRContext *ctx, const char *source_buffer)`**
  这是 `ir/parser.h` 提供的**主入口点**。
  
  * **输入**:
    * `ctx`: 你在上一步创建的上下文。
    * `source_buffer`: 一个标准的 C 字符串（`const char *`），包含你想解析的 `.cir` 文本。
  * **输出**:
    * **成功**: 返回一个指向新创建的 `IRModule` 对象的指针。
    * **失败**: 返回 `NULL`。**重要的是**，它还会在 `stderr` 上自动打印一个格式精美的错误信息，指出失败的**确切行号和列号**。

* **`bool ir_verify_module(IRModule *mod)`**
  这是一个诊断工具，用于检查 `IRModule` 是否遵循了 `calir` 的所有规则（例如 SSA 规则、类型匹配等）。`ir_parse_module` 在返回前会自动调用它，但你也可以在手动修改 IR 后再次调用它以确保正确性。

* **`void ir_context_destroy(IRContext *ctx)`**
  释放 `IRContext` 及其拥有的所有相关内存（包括 `IRModule`、`IRFunction`、`IRType` 等）。

## 2.4. 编译与运行

由于你已经构建了 `libcalico.a`（在上一章 `01_build_and_test.md` 中），我们现在只需要编译 `my_parser_test.c` 并将其链接到你的库即可。

1. **编译程序**（在 `calico` 根目录运行）：
   
   ```bash
   clang -std=c23 -g -Wall -Iinclude -o build/my_parser_test my_parser_test.c -Lbuild -lcalico -lm
   ```
   
   * `-Iinclude`：告诉编译器在哪里查找 `"ir/parser.h"` 等头文件。
   * `-Lbuild`：告诉链接器在哪里查找库。
   * `-lcalico`：链接 `libcalico.a` 库（`calico` 是 `libcalico.a` 的简写）。
   * `-lm`：链接数学库（`Makefile` 中也包含了）。

2. **运行程序**：
   
   ```bash
   ./build/my_parser_test
   ```

3. **预期输出**：
   
   ```
   正在解析 IR...
   --- 解析成功 ---
   模块名: parsed_module
   模块已验证通过。
   ```

## 2.5. 强大的错误报告

`Calico` 解析器的真正亮点在于它如何处理错误。让我们故意破坏 `CIR_SOURCE` 字符串。

**修改** `my_parser_test.c` 中的 `%sum: i32 = add ...` 这一行，**删除类型注解**：

```c
// ...
    "define i32 @add(%a: i32, %b: i32) {\n"
    "$entry:\n"
    "  %sum = add %a: i32, %b: i32\n"  // 错误！删除了 : i32
    "  ret %sum: i32\n"
    "}\n";
// ...
```

现在，**重新编译并运行**（使用与上一步相同的命令）。你不需要在 `main` 函数中添加任何新的错误处理代码，解析器会自动为你完成：

1. `make build/my_parser_test` （或者直接运行 `clang` 命令）
2. `./build/my_parser_test`

**错误输出**：

```
正在解析 IR...

--- Parse Error ---
Error: 5:3: Expected ':', but got '='
  |
5 |   %sum = add %a: i32, %b: i32
  |       ^

Failed to parse IR.
测试失败。
```

注意 `ir_parse_module` 如何返回 `NULL`，导致 `main` 函数打印 "测试失败。"，但更重要的是，`ir_parse_module` **自己**打印了一个详细的、带上下文的错误信息，准确地指出了问题所在。

## 2.6. 下一步

你已经成功地将文本 IR 解析为了内存中的对象！从这里开始，你有两个主要的选择：

* [-\> 教程：执行你的 IR](03_tutorial_interpreter.md)(学习如何使用 `interpreter` 运行这个 `@add` 函数并得到 `30`)
* [-\> 指南：使用 Builder API 构建 IR](../how-to-guides/01_build_with_builder.md)(学习如何不使用文本，而是通过 C 函数调用来构建 IR)

<!-- end list -->

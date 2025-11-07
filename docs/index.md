# 欢迎来到 Calico 文档

**一个轻量级、零依赖、受 LLVM 启发的编译器工具套件，使用纯 C (C23) 编写，包含完整的 IR、分析遍和解释器。**

`Calico` 是一个用于构建通用编译器后端的个人项目，源于 UCAS 的“编译原理”课程作业。它围绕一个受 LLVM 启发的轻量级中间表示（IR）—— **`calir`** ——构建。

本项目提供了定义、构建、解析、分析、转换、验证和**执行** SSA 形式 IR 所需的核心数据结构、转换遍、分析遍和解释器。

## 为什么选择 Calico？

厌倦了 LLVM 框架中千万行级别的 C++ 代码？Calico 专为学习、原型设计和教学而生。

* **轻量且易于理解：** 整个框架小巧、自包含，并包含大量注释。它不仅是用来“用”的，更是用来“学”的。
* **纯 C (C23) 且零依赖：** 没有 C++，没有复杂的构建系统，没有外部库。你只需要 `make` 和一个兼容 C23 的编译器。
* **功能齐全的核心：** 别让“轻量级”欺骗了你。Calico 包含：
  * 一个健壮的 `IRBuilder` API
  * 一个完整的**文本 IR 解析器**（`.cir` 文件），具有详细的行级错误报告
  * 一个用于立即执行的**树遍历 IR 解释器**
  * 一个严格的 **SSA 与类型校验器**
  * 经典的**支配点分析** (Lengauer-Tarjan) 和支配前沿计算
  * 完整的 **`mem2reg`** 遍，用于构建 SSA

---

## 快速上手 1: 解析文本 IR ("Hello, World!")

Calico 可以解析、验证和打印 `.cir` 文本文件，并提供强大的错误报告。

```c
#include "ir/context.h"
#include "ir/module.h"
#include "ir/parser.h"
#include "ir/verifier.h"
#include <stdio.h>

// 我们的 IR 源码 (例如在 .cir 文件中)
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

  // 1. 解析
  IRModule *mod = ir_parse_module(ctx, CIR_SOURCE);

  if (mod == NULL) {
    fprintf(stderr, "解析 IR 失败。\n");
    ir_context_destroy(ctx);
    return 1;
  }
  printf("解析成功。模块: %s\n", mod->name);

  // 2. 校验 (可选，但推荐)
  if (ir_verify_module(mod)) {
    printf("模块校验成功。\n");
  }

  ir_context_destroy(ctx);
  return 0;
}
```

### 强大的错误报告

如果 IR 包含语法错误（例如，`%sum = add` 而不是 `%sum: i32 = add`），解析器将精确定位错误：

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

## 快速上手 2: 使用 C API 构建 IR

除了直接解析文本，你还可以使用 `IRBuilder` API 以编程方式构建复杂的 IR。

### 目标 IR

这是我们希望通过 C API 构建出的 `.cir` 文本。它使用了命名结构体、`alloca` 和 `gep` (Get Element Pointer) 指令。

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

### Builder C 代码

`IRBuilder` API 提供了一套流畅的接口，用于创建类型、常量、函数、基本块和指令，并自动处理 Use-Def 链。这对于动态生成代码或在其他语言中实现前端绑定至关重要。

我们提供了一个完整的教程，展示了如何使用 C API 生成上述的目标 IR。

**[-\> 查看完整的 IRBuilder 构建指南](how-to-guides/01_build_with_builder.md)**

-----

## 快速上手 3: 使用解释器执行 IR

Calico 包含一个简单的树遍历解释器，可以直接执行 `calir` IR。这对于测试、调试，甚至将 `calir` 用作脚本后端都非常完美。

你可以解析 "Hello, World\!" 示例，然后直接在内存中执行 `@add` 函数：

```
$ ./my_interpreter_test
Result of @add(10, 20): 30
```

我们提供了一个完整的教程，展示了如何加载 IR 模块、查找函数、准备运行时参数并调用解释器。

**[-\> 查看完整的解释器执行教程](getting-started/03_tutorial_interpreter.md)**

-----

## 核心功能

* **解释器 (`interpreter/`)**: 一个树遍历解释器，能够执行 `calir` IR，包含栈和堆管理，用于调试和测试。
* **IR 核心 (`ir/`)**: 健壮的 **Use-Def 链**实现，丰富的类型系统（原生类型、指针、数组、命名/匿名结构体），用于**类型/常量/字符串池化**的中央 `IRContext`，以及功能完备的 `IRBuilder` API (`alloca`, `load`, `store`, `gep`, `phi` 等)。
* **文本 IR (`ir/`)**: 完整的**文本 IR 解析器** (`ir_parse_module`) 和 **IR 打印器** (`IRPrinter`)，用于将 IR 序列化到文件 (`.cir`)、`stdout` 或字符串。
* **校验器 (`ir/`)**: 一个关键的 **IR 校验器** (`ir_verify_module`)，用于检查正确性（例如 SSA 支配规则、类型匹配）。
* **分析 (`analysis/`)**: 包括**控制流图 (CFG)** 生成、**支配树**计算 (Lengauer-Tarjan) 和**支配前沿**计算。
* **转换 (`transforms/`)**: 实现了经典的\*\*“内存到寄存器” (`mem2reg`)\*\* 遍，用于将 `alloca`/`load`/`store` 提升为 SSA 形式的 `phi` 节点。
* **工具集 (`utils/`)**: 高性能的辅助工具，包括**碰撞（Bump）分配器**、侵入式链表、通用哈希表和位图。

## 构建与测试

### 依赖

* `make`
* 兼容 C23 的编译器 (例如 `gcc` 或 `clang`)

### 运行

(由 `Makefile` 自动管理)

1. **构建并运行所有测试 (推荐):**
   
   ```bash
   make test
   ```

2. **构建并运行特定测试 (例如 `test_parser`):**
   
   ```bash
   make run_test_parser
   ```

## 路线图

核心 IR、遍和解释器已趋于稳定。未来的目标是构建一个针对 RISC-V 的高质量原生后端。

* [x] **IR 解释器 (`interpreter/`)**
  * **(已完成)** 用于调试 IR 的树遍历解释器。
* [x] **IR 文本解析器 (`ir/parser`)**
  * **(已完成)**
* [x] **核心指令集 (`ir/*`)**
  * **(已完成)**
* [x] **支配前沿 (`analysis/dom_frontier`)**
  * **(已完成)**
* [x] **Mem2Reg 遍 (`transforms/mem2reg`)**
  * **(已完成)**
* [ ] **后端: RISC-V (当前目标)**
  * [ ] **指令选择:** 使用虚拟寄存器将 `calir` (IR) 翻译为 `MachineInstr` (MIR)。
  * [ ] **寄存器分配 (图着色):**
    * [ ] **活跃度分析:** 为所有虚拟寄存器构建活跃区间。
    * [ ] **冲突图:** 构建寄存器冲突图。
    * [ ] **着色/溢出:** 实现 Chaitin-Briggs 图着色算法和溢出代码生成。
  * [ ] **MC/ELF 发射器:**
    * [ ] **指令编码:** 实现 RISC-V `MachineInstr` 的二进制编码。
    * [ ] **目标文件发射器:** 生成包含 `.text`, `.symtab`, `.rela.text` 等节的可重定位 ELF (`.o`) 文件。
* [ ] **简单优化 (`transforms/*`)**
  * [ ] 常量折叠
  * [ ] 死代码消除 (DCE)

## 贡献

我们热烈欢迎各种贡献、问题和反馈！请参阅 [CONTRIBUTING.md](CONTRIBUTING.md) 了解详细指南。

## 许可证

本项目采用 Apache-2.0 许可证。详情请见 [LICENSE](../LICENSE) 和 [NOTICE](../NOTICE) 文件。



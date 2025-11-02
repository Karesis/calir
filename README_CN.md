# Calico-IR

**一个受 LLVM 启发的、用 C 语言编写的跨平台中间表示 (IR) 框架。**

`Calico-IR` (或 `calir`) 是一个用于构建通用编译器后端的个人项目。它提供了定义、构建、解析、分析、转换和验证 SSA 形式 IR 所需的核心数据结构、转换和分析遍 (Pass)。

本项目是 UCAS "编译原理" 课程作业的一部分。

## 核心特性

`Calico-IR` 的设计在逻辑上分为五个层次：

  * **`utils/` (核心工具层)**

      * `bump.c`: 高性能的 **Bump 内存分配器**，用于 IR 对象的快速分配和生命周期管理。
      * `hashmap.c`: 类型安全、高性能的通用哈希表。
      * `id_list.h`: 用于管理 IR 对象（如指令、基本块）的**侵入式链表**。
      * `string_buf.h` / `temp_vec.h`: 专为 Bump 分配器优化的动态集合（动态字符串和 `void*` 向量）。
      * `bitset.c`: 用于数据流分析的位集库。

  * **`ir/` (IR 核心层)**

      * `context.h`: 强大的 **`IRContext`** 作为中心管理器，负责：
          * **类型驻留** (指针、数组、结构体)。
          * **常量驻留** (整数、浮点数、`undef`、`null`)。
          * **字符串驻留** (用于标识符)。
      * `value.h` / `use.h`: 健壮的 **Use-Def 链**实现。
      * `type.h`: 丰富的类型系统，支持原始类型、指针、数组以及命名/匿名结构体。
      * `builder.h`: 功能完备的 **`IRBuilder` API**，用于在 C 语言中以编程方式安全地构建 IR，支持 `alloca`, `load`, `store`, `gep`, `phi` 等复杂指令。
      * `lexer.h` / `parser.h`: 一个完整的**文本 IR 解析器** (`ir_parse_module`)，能够解析 LLVM 风格的 `.calir` 文本文件并将其转换回内存中的 IR。
      * `error.h`: 配合解析器，提供**精确到行列**的详细错误报告。
      * `printer.h`: 灵活的 **IR 打印器** (`IRPrinter`)，可将内存中的 IR 序列化为文件、`stdout` 或字符串。
      * `verifier.h`: 关键的 **IR 验证器** (`ir_verify_module`)，用于检查 IR 的正确性（例如 SSA 支配规则、类型匹配等）。

  * **`analysis/` (分析层)**

      * `cfg.h`: **控制流图 (CFG)** 生成。
      * `dom_tree.h`: 基于 Lengauer-Tarjan 算法的**支配树**计算。
      * `dom_frontier.h`: **支配边界**计算（SSA 构建的先决条件）。

  * **`transforms/` (转换层)**

      * `mem2reg.h`: 经典的 **"Memory to Register"** 转换遍，用于将 `alloca`/`load`/`store` 提升为 SSA 形式的 `phi` 节点。

  * **`interpreter/` (解释器层)**

      * `interpreter.h`: (正在开发) 一个用于执行 IR 的**树遍历解释器**。

-----

## 快速入门

`Calico-IR` 提供了两个主要的入口点：通过 `IRBuilder` **构建** IR，或通过 `ir_parse_module` **解析** IR。

### 示例 1: 使用 Builder API 构建 IR

下面的 C 代码展示了如何使用 `IRBuilder` API 以编程方式构建一个包含命名结构体、全局变量、`alloca` 和 `gep` 的模块。

#### 示例 C 代码 (`tests/test_readme_example.c`)

```c
/*
 * (这是一个经过验证的测试用例)
 * 目标: 构建一个使用 GEP 访问结构体和数组的函数
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

// 构建示例 IR 结构
static void
build_readme_ir(IRModule *mod)
{
  IRContext *ctx = mod->context;

  // 1. 获取/创建类型
  IRType *i32_type = ir_type_get_i32(ctx);
  IRType *i64_type = ir_type_get_i64(ctx);
  IRType *void_type = ir_type_get_void(ctx);

  // %point = type { i32, i64 }
  IRType *point_members[2] = {i32_type, i64_type};
  IRType *point_type = ir_type_get_named_struct(ctx, "point", point_members, 2);

  // 匿名数组类型: [10 x i32]
  IRType *array_type = ir_type_get_array(ctx, i32_type, 10);

  // %data_packet = type { %point, [10 x i32] }
  IRType *packet_members[2] = {point_type, array_type};
  IRType *data_packet_type = ir_type_get_named_struct(ctx, "data_packet", packet_members, 2);

  // 2. 创建全局变量
  // @g_data = ...
  ir_global_variable_create(mod,
                            "g_data",   // 名称
                            array_type, // 类型
                            NULL);      // 初始值 (NULL = zeroinitializer)

  // 3. 创建函数和入口
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

  // 5. 创建 GEP 和 Store
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

  // 6. 终结者
  ir_builder_create_ret(builder, NULL); // ret void
  ir_builder_destroy(builder);
}

// Main 函数
int
main()
{
  IRContext *ctx = ir_context_create();
  IRModule *mod = ir_module_create(ctx, "test_module");

  // 构建 IR
  build_readme_ir(mod);

  // 将 IR 打印到 stdout
  printf("--- Calir IR Dump ---\n");
  ir_module_dump_to_file(mod, stdout);
  printf("--- Dump Complete ---\n");

  ir_context_destroy(ctx);
  return 0;
}
```

#### 预期输出 (`ir_module_dump_to_file`)

```llvm
--- Calir IR Dump ---
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
--- Dump Complete ---
```

-----

### 示例 2: 解析和验证文本 IR

`ir/parser` 模块可以将文本 IR 解析回内存对象，并提供详细的错误报告。

```c
#include "ir/context.h"
#include "ir/module.h"
#include "ir/parser.h"
#include "ir/verifier.h"
#include <stdio.h>

// 我们的 IR 源文件
const char *IR_SOURCE =
    "module = \"parsed_module\"\n"
    "\n"
    "define i32 @add(%a: i32, %b: i32) {\n"
    "$entry:\n"
    "  %sum: i32 = add %a: i32, %b: i32\n"
    "  ret %sum: i32\n"
    "}\n";

int
main()
{
  IRContext *ctx = ir_context_create();
  IRModule *mod = ir_parse_module(ctx, IR_SOURCE);

  if (mod == NULL)
  {
    fprintf(stderr, "Failed to parse IR.\n");
    ir_context_destroy(ctx);
    return 1;
  }
  printf("Parse successful. Module: %s\n", mod->name);

  if (ir_verify_module(mod))
  {
    printf("Module verified successfully.\n");
  }

  ir_context_destroy(ctx);
  return 0;
}
```

#### 解析器错误报告

如果 IR 存在语法错误（例如 `%sum = add` 而不是 `%sum: i32 = add`），解析器会精确定位错误：

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

## 构建和测试

### 依赖项

  * `make`
  * C 编译器 (例如 `gcc` 或 `clang`)

### 运行

(由 `Makefile` 自动管理)

1.  **构建并运行所有测试 (推荐):**

    ```bash
    make test
    ```

2.  **构建并运行一个特定测试 (例如 `test_parser`):**

    ```bash
    make run_test_parser
    ```

3.  **仅构建一个特定测试 (不运行):**

    ```bash
    make build/test_parser
    ```

-----

## 项目状态

**本项目功能齐全，特性丰富。** 核心的 IR 管道（构建、解析、验证、转换）均已实现并经过测试。`mem2reg` 转换遍的完成标志着 SSA 构建流程已准备就绪。

欢迎提出 Issues 和反馈！

## 路线图

核心框架已搭建完毕，后续目标是实现解释器和更多的优化遍。

  * [ ] **IR 解释器 (`ir/interpreter`)**
      * **(当前目标)** 实现一个用于执行和调试 IR 的树遍历解释器。
  * [x] **IR 文本解析器 (`ir/parser`)**
      * 已完成。能够解析 `.calir` 文本文件并提供详细错误报告。
  * [x] **支配边界 (`analysis/dom_frontier`)**
      * 已完成。SSA 构建的先决条件。
  * [x] **Mem2Reg Pass (`transforms/mem2reg`)**
      * 已完成。实现 `alloca`/`load`/`store` 到 `phi` 节点的提升。
  * [ ] **简单优化 (`transforms/*`)**
      * [ ] 常量折叠 (Constant Folding)
      * [ ] 死代码消除 (DCE)

## 许可证

本项目基于 Apache-2.0 许可证 - 详情请参见 LICENSE 文件。
# 指南：使用 IRBuilder 构建 IR

在“入门指南”中，我们学习了如何*解析*和*执行*已有的 `.cir` 文本。现在，我们将学习如何从零开始，通过 C API **程序化地构建** IR。

这是 `Calico` 的核心功能之一，对于编写编译器前端（例如，将你的 `nyan` 语言降级为 `calir`）或动态生成代码至关重要。

本指南的核心 API 是 `ir/builder.h` 和 `ir/context.h`。

## 3.1. 目标：我们要构建什么？

我们的目标是使用 C API 完整地构建出以下 `.cir` 文本所代表的 IR 模块。

这个模块包含：

* 命名结构体 (`%point`, `%data_packet`)
* 数组类型 (`[10 x i32]`)
* 全局变量 (`@g_data`)
* 一个使用 `alloca`, `gep` 和 `store` 的函数

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

## 3.2. IRBuilder 工作流

使用 `IRBuilder` 的标准流程如下：

1. **获取 `IRContext`**: 这是所有类型和常量的“工厂”。
2. **创建 `IRModule`**: 作为所有全局变量和函数的容器。
3. **获取/创建 `IRType`**: 从 `IRContext` 中获取 `i32`、`void` 等类型，并定义结构体。
4. **创建 `IRFunction` 和 `IRBasicBlock`**: 定义函数签名并创建其入口基本块。
5. **创建 `IRBuilder`**: 实例化构建器。
6. **设置插入点**: `ir_builder_set_insertion_point(builder, bb)`。**这是最关键的一步**。
7. **构建指令**: 调用 `ir_builder_create_alloca`, `ir_builder_create_gep` 等。指令会被自动插入到 `insertion_point` 的末尾。
8. **（可选）打印模块**: 使用 `ir_module_dump_to_file` 验证结果。

## 3.3. 完整的 C 代码示例

以下代码（源自 `tests/test_readme_example.c`）完整演示了上述工作流，用于生成我们的“目标 IR”。

```c
/*
 * 目标：构建一个使用 GEP 访问结构体和数组的函数
 */
#include "ir/builder.hh"
#include "ir/context.h"
#include "ir/function.h"
#include "ir/global.h"
#include "ir/module.h"
#include "ir/type.h"
#include "ir/basicblock.h"
#include "ir/constant.h"
#include "ir/argument.h"
#include <stdio.h> // 用于打印

/**
 * @brief 核心的 IR 构建逻辑
 * @param mod 要在其中构建的模块
 */
static void
build_readme_ir(IRModule *mod)
{
  IRContext *ctx = mod->context;

  // --- 1. 获取/创建类型 (来自 context.h) ---
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

  // --- 2. 创建全局变量 (来自 global.h) ---
  // @g_data = global [10 x i32] zeroinitializer
  ir_global_variable_create(mod,
                            "g_data",   // 名字
                            array_type, // 类型
                            NULL);      // 初始化器 (NULL = zeroinitializer)

  // --- 3. 创建函数和入口块 (来自 function.h / basicblock.h) ---
  // define void @test_func(%idx: i32)
  IRFunction *func = ir_function_create(mod, "test_func", void_type);
  IRArgument *arg_idx_s = ir_argument_create(func, i32_type, "idx");
  ir_function_finalize_signature(func, false); // 锁定函数签名
  IRValueNode *arg_idx = &arg_idx_s->value; // 获取参数的 ValueNode

  IRBasicBlock *entry_bb = ir_basic_block_create(func, "entry");
  ir_function_append_basic_block(func, entry_bb);

  // --- 4. 创建并设置 Builder (来自 builder.h) ---
  IRBuilder *builder = ir_builder_create(ctx);
  ir_builder_set_insertion_point(builder, entry_bb); // 关键！

  // --- 5. 构建 Alloca (内存分配) ---
  // %packet_ptr: <%data_packet> = alloc %data_packet
  IRValueNode *packet_ptr =
      ir_builder_create_alloca(builder, data_packet_type, "packet_ptr");

  // --- 6. 获取常量 (来自 constant.h) ---
  IRValueNode *const_0 = ir_constant_get_i32(ctx, 0);
  IRValueNode *const_1 = ir_constant_get_i32(ctx, 1);
  IRValueNode *const_123 = ir_constant_get_i32(ctx, 123);

  // --- 7. 构建 GEP (指针计算) ---
  // %elem_ptr: <i32> = gep inbounds %packet_ptr: <%data_packet>, 0: i32, 1: i32, %idx: i32
  IRValueNode *gep_indices[] = {const_0, const_1, arg_idx};
  IRValueNode *elem_ptr =
      ir_builder_create_gep(builder,
                            data_packet_type, // GEP 基于的源类型
                            packet_ptr,       // GEP 基地址
                            gep_indices,      // 索引数组
                            3,                // 索引数量
                            true /* inbounds */,
                            "elem_ptr");

  // --- 8. 构建 Store (内存写入) ---
  // store 123: i32, %elem_ptr: <i32>
  ir_builder_create_store(builder, const_123, elem_ptr);

  // --- 9. 构建终结者 (Terminator) ---
  ir_builder_create_ret(builder, NULL); // ret void

  // --- 10. 清理 ---
  ir_builder_destroy(builder);
}

// -----------------------------------------------------------------
// --- Main 函数：设置、构建和打印 ---
// -----------------------------------------------------------------
int
main()
{
  // 1. 设置
  IRContext *ctx = ir_context_create();
  IRModule *mod = ir_module_create(ctx, "test_module");

  // 2. 调用我们的构建函数
  build_readme_ir(mod);

  // 3. 打印结果到 stdout
  printf("--- Calico IR Dump ---\n");
  // ir_module_dump_to_file 是一个在 ir/printer.h 中的辅助函数
  ir_module_dump_to_file(mod, stdout);
  printf("--- Dump Complete ---\n");

  // 4. 清理
  ir_context_destroy(ctx);
  return 0;
}
```

## 3.4. 关键 API 讲解

* **`IRBuilder *ir_builder_create(IRContext *ctx)`**
  创建一个新的 `IRBuilder`。它只持有一个 `IRContext` 的引用。

* **`void ir_builder_set_insertion_point(IRBuilder *builder, IRBasicBlock *bb)`**
  `IRBuilder` 中**最重要**的函数。它告诉构建器：“你现在插入的所有新指令都应该被添加到 `bb` 这个基本块的末尾。”

* **`ir_builder_create_...(IRBuilder *builder, ...)`** (例如 `_alloca`, `_gep`, `_ret`)
  这些是 `IRBuilder` 的核心。它们负责：
  
  1. 创建指令对象（在 `ctx->ir_arena` 中分配）。
  2. 设置指令的操作数（Use-Def 链）。
  3. 将指令**自动插入**到当前设置的 `insertion_point`。
  4. 返回一个指向指令结果的 `IRValueNode *`（如果指令有结果）。

* **`ir_type_get_...(IRContext *ctx, ...)`** (例如 `_i32`, `_get_ptr`, `_get_named_struct`)
  这些是类型工厂。`IRContext` 确保类型是**唯一**的（类型池化/interning）。如果你两次请求 `ir_type_get_i32(ctx)`，你会得到指向**同一个** `IRType` 对象的指针。

* **`ir_constant_get_...(IRContext *ctx, ...)`** (例如 `_i32`)
  常量工厂，与类型工厂类似。`ir_constant_get_i32(ctx, 0)` 总是返回指向同一个 `i32 0` 常量值的指针。

## 3.5. 如何编译与运行

这个示例代码已经作为 `Calico` 项目的官方测试用例之一存在于 `tests/test_readme_example.c` 中。

你不需要手动编译它。你可以在项目根目录中直接使用 `make` 来运行它：

```bash
make run_test_readme_example
```

你将看到与“目标 IR”部分完全匹配的 `stdout` 输出。

## 3.6. 下一步

你已经掌握了如何以编程方式构建 IR！

**[-\> 下一篇：指南：如何运行分析遍 (Analysis Pass)](02_how_to_run_analysis.md)**

# 3. 教程：执行你的 IR (解释器)

欢迎来到 `Getting Started` 系列的最后一篇教程！

在上一章，我们学习了如何将 `.cir` 文本字符串解析为内存中的 `IRModule`。在这一章，我们将更进一步：我们将从该模块中提取 `@add` 函数，并**实际执行它**以获得计算结果。

本教程的核心 API 来自 `interpreter/interpreter.h` 和 `utils/data_layout.h`。

## 3.1. 关键 API 概览

* **`DataLayout *datalayout_create_host(void)`**
    解释器需要知道目标机器的类型大小和对齐方式（例如，`i32` 占 4 字节，`ptr` 占 8 字节）。这个辅助函数（来自 `utils/data_layout.h`）会创建一个代表**当前运行机器**（“宿主”）的数据布局。

* **`Interpreter *interpreter_create(DataLayout *data_layout)`**
    创建解释器“引擎”。它是一个长时对象，持有 FFI 函数表和全局变量内存。它会**借用**你传入的 `DataLayout`。

* **`RuntimeValue`** (结构体)
    这是解释器“世界”的“货币”。它是一个带标签的联合体（Tagged Union），用于在 C 代码和 `Calico` IR 之间传递数据。
  
  ```c
  RuntimeValue rt_val;
  rt_val.kind = RUNTIME_VAL_I32; // 1. 设置类型标签
  rt_val.as.val_i32 = 10;        // 2. 设置对应的值
  ```

* **`bool interpreter_run_function(...)`**
    执行一个 `IRFunction` 的主 API。

## 3.2. 完整的 C 代码

我们将复用上一篇教程的代码，并在其基础上增加解释器逻辑。

创建 `my_interp_test.c` 文件：

```c
/* my_interp_test.c */

#include <stdio.h>
#include <string.h> // 需要用 strcmp 查找函数

/* Calico 核心头文件 */
#include "ir/context.h"
#include "ir/function.h"
#include "ir/module.h"
#include "ir/parser.h"
#include "utils/id_list.h" // 需要用 list_for_each

/* 解释器的新头文件 */
#include "interpreter/interpreter.h"
#include "utils/data_layout.h"

// 我们的目标 IR 源码 (与上一篇教程相同)
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
  DataLayout *dl = datalayout_create_host(); // 1. 创建宿主数据布局
  Interpreter *interp = interpreter_create(dl); // 2. 创建解释器
  IRModule *mod = NULL;

  if (interp == NULL) {
    fprintf(stderr, "创建解释器失败。\n");
    goto cleanup;
  }

  // --- 3. 解析 (与上一篇教程相同) ---
  mod = ir_parse_module(ctx, CIR_SOURCE);
  if (mod == NULL) {
    fprintf(stderr, "解析 IR 失败。\n");
    goto cleanup;
  }
  printf("IR 解析成功。\n");

  // --- 4. 查找 @add 函数 ---
  IRFunction *add_func = NULL;
  IDList *it;
  list_for_each(&mod->functions, it) {
    IRFunction *f = list_entry(it, IRFunction, list_node);
    // 注意：f->entry_address.name 不包含 '@'
    if (strcmp(f->entry_address.name, "add") == 0) {
      add_func = f;
      break;
    }
  }

  if (add_func == NULL) {
    fprintf(stderr, "在模块中未找到 '@add' 函数。\n");
    goto cleanup;
  }
  printf("找到了 '@add' 函数。\n");

  // --- 5. 准备输入参数 (10 和 20) ---
  RuntimeValue rt_a;
  rt_a.kind = RUNTIME_VAL_I32;
  rt_a.as.val_i32 = 10;

  RuntimeValue rt_b;
  rt_b.kind = RUNTIME_VAL_I32;
  rt_b.as.val_i32 = 20;

  // API 需要一个 *指针数组*
  RuntimeValue *args[] = {&rt_a, &rt_b};

  // --- 6. 执行函数 ---
  RuntimeValue result; // 用于接收返回值的栈上变量
  printf("正在执行 @add(10, 20)...\n");

  bool success = interpreter_run_function(interp,
                                          add_func, // 要运行的函数
                                          args,     // 参数的指针数组
                                          2,        // 参数数量
                                          &result); // [out] 接收结果

  // --- 7. 打印结果 ---
  if (success && result.kind == RUNTIME_VAL_I32) {
    printf("--- 执行成功 ---\n");
    printf("Result: %d\n", result.as.val_i32);
  } else {
    fprintf(stderr, "--- 执行失败! ---\n");
    // (注意：解释器在失败时会自动打印详细错误)
  }

cleanup:
  // 7. 清理 (注意顺序)
  // 解释器在 DataLayout 和 Context 之前销毁
  if (interp) interpreter_destroy(interp);
  if (dl) datalayout_destroy(dl);
  if (ctx) ir_context_destroy(ctx); // Context 最后销毁 (因为它拥有 Module)

  return !success;
}
```

## 3.3. 编译与运行

由于 `interpreter` 的代码已经编译进了 `libcalico.a`，**编译命令与上一篇教程完全相同**。

1. **编译程序**（在 `calico` 根目录运行）：
   
   ```bash
   clang -std=c23 -g -Wall -Iinclude -o build/my_interp_test my_interp_test.c -Lbuild -lcalico -lm
   ```

2. **运行程序**：
   
   ```bash
   ./build/my_interp_test
   ```

3. **预期输出**：
   
   ```
   IR 解析成功。
   找到了 '@add' 函数。
   正在执行 @add(10, 20)...
   --- 执行成功 ---
   Result: 30
   ```

## 3.4. API 深入讲解

我们来回顾一下关键的解释器 API：`interpreter_run_function`。

```c
bool interpreter_run_function(
    Interpreter *interp,     // 解释器实例
    IRFunction *func,        // 你想运行的 IRFunction
    RuntimeValue **args,     // [in]  一个指向 RuntimeValue 的指针数组
    size_t num_args,         //       数组中的参数数量
    RuntimeValue *result_out // [out] 指向一个用于写入返回值的 RuntimeValue
);
```

* **`RuntimeValue **args`**: 这是最容易出错的地方。
  你不能传入一个 `RuntimeValue` 数组 (`RuntimeValue args[]`)。
  你必须在栈上创建 `RuntimeValue`，然后创建一个**指向它们的指针**所组成的数组 (`RuntimeValue *args[] = {&rt_a, &rt_b};`)。

* **`RuntimeValue *result_out`**:
  这是一个“输出参数”。你需要在栈上声明一个 `RuntimeValue result;`，然后将**它的地址 `&result`** 传递给函数。如果函数成功执行并返回（`ret`），`interpreter` 就会把返回值复制到你的 `result` 变量中。

## 3.5. 恭喜！

你已经完成了 `Getting Started`（入门指南）系列！

你学习了：

1. **构建**: 如何配置 `Clang 20+` 并使用 `make test` 验证环境。
2. **解析**: 如何使用 `ir_parse_module` 将 `.cir` 文本转换为 `IRModule`。
3. **执行**: 如何使用 `interpreter_run_function` 运行 IR 并获取结果。

-----

## 下一步

你已经掌握了如何*使用* `Calico`，下一步是学习如何*创建* IR。

**[-\> 下一篇：操作指南：使用 `IRBuilder` 构建 IR](../how-to-guides/01_build_with_builder.md)**

# 指南：如何验证 IR

你已经学会了如何使用 `IRBuilder` 构建 IR，但你怎么知道你构建的 IR 是“正确的”？

`Calico` IR 遵循一套严格的规则（例如 SSA）。`Verifier` (校验器) 就是你的“安全网”，它是一个强大的诊断工具，用于在运行时捕获 IR 错误。

本指南的核心 API 是 `ir/verifier.h`。

## 2.1. 校验器会检查什么？

`ir_verify_module` 和 `ir_verify_function` 会执行一系列深度检查，包括（但不限于）：

* **SSA 支配规则**: 确保一个指令的操作数（如果本身是指令）**支配**（dominates）该指令。这是 SSA 最核心的规则。
* **终结者规则**: 确保每个基本块都**恰好**以一个终结者指令（`ret`, `br`, `cond_br`, `switch`）结尾，并且终结者只出现在末尾。
* **PHI 节点规则**:
  * 确保 `phi` 指令只出现在基本块的**开头**。
  * 确保 `phi` 节点为**每一个**CFG 前驱（predecessor）都包含一个（且仅一个）条目。
* **类型一致性**: 确保所有操作（例如 `add`, `load`, `store`）的操作数类型和结果类型都是正确的。
* **Use-Def 链**: 确保 Use-Def 链的双向链接是完整的。
* **Alloca 规则**: 确保 `alloca` 指令只出现在函数的**入口块**中。

## 2.2. 什么时候应该调用它？

1. **在 `ir_parse_module` 之后？ (不需要)**
   
   * `ir_parse_module` 函数（如“教程 2”所示）**已经自动**在内部调用了 `ir_verify_module`。如果解析出的 IR 是无效的，`ir_parse_module` 会自动打印错误并返回 `NULL`。你无需额外操作。

2. **在 `IRBuilder` 构建之后？ (强烈推荐)**
   
   * **这是最主要的用例。** 当你使用 `IRBuilder` 手动创建指令时，你可能会无意中违反上述规则。在构建完函数或模块后，立即调用 `ir_verify_module` 是一个好习惯。

3. **在编写转换 (Transform) Pass 时？ (必需)**
   
   * 在你的 Pass 运行*之前*调用 `ir_verify_function`，以断言你收到的 IR 是合法的。
   * 在你的 Pass 运行*之后*调用 `ir_verify_function`，以确保你生成的 IR 也是合法的。

## 2.3. 如何使用

API 非常简单：

* `bool ir_verify_module(IRModule *mod)`
* `bool ir_verify_function(IRFunction *func)`

它们在成功时返回 `true`。如果失败，它们会返回 `false`，并**自动向 `stderr` 打印一份详细的、带上下文的错误报告**。

### 示例：验证我们的 Builder 模块

让我们把校验器添加到上一篇指南（`01_build_with_builder.md`）的代码中。

```c
/* * 摘自 01_build_with_builder.md 的 main 函数
 */
#include "ir/verifier.h" // 1. 包含头文件
#include <stdio.h>
// ... (其他 includes)

int
main()
{
  // 1. 设置
  IRContext *ctx = ir_context_create();
  IRModule *mod = ir_module_create(ctx, "test_module");

  // 2. 调用我们的构建函数
  build_readme_ir(mod); // (来自上一篇指南)

  // 3. 打印结果到 stdout
  printf("--- Calico IR Dump ---\n");
  ir_module_dump_to_file(mod, stdout);
  printf("--- Dump Complete ---\n");

  // --- 4. 验证！ ---
  printf("Verifying module...\n");
  if (ir_verify_module(mod)) {
    printf("[OK] Module verified successfully.\n");
  } else {
    fprintf(stderr, "[ERROR] Module verification FAILED.\n");
  }
  // -------------------

  // 5. 清理
  ir_context_destroy(ctx);
  return 0;
}
```

由于我们上一篇指南中的 `build_readme_ir` 函数是正确的，运行它将输出：

```
...
--- Dump Complete ---
Verifying module...
[OK] Module verified successfully.
```

## 2.4. 示例：失败的校验 (SSA 冲突)

假设你尝试构建一个违反 SSA 支配规则的 IR。

**错误的 `.cir` 示例：**

```llvm
define i32 @bad_ssa(%cond: i1) {
$entry:
  br %cond: i1, $then, $else
$then:
  %x: i32 = 10: i32
  br $end
$else:
  br $end
$end:
  ; %x 在这里是未定义的，因为它不支配 $end 块
  %res: i32 = add %x: i32, 1: i32  ; <-- 校验器将在此处失败
  ret %res: i32
}
```

如果你用 `IRBuilder` 构建了上述 IR，然后调用 `ir_verify_function`，你**不会**得到一个“段错误”。相反，你会得到一个清晰的错误报告，自动打印到 `stderr`，内容大致如下：

```
--- [CALIR VERIFIER ERROR] ---
At:          src/ir/verifier.c:354
In Function: bad_ssa
In Block:    $end
Error:       SSA VIOLATION: Definition in block '$then' does not dominate use in block '$end'.
Object:      %res: i32 = add %x: i32, 1: i32
---------------------------------
[ERROR] Module verification FAILED.
```

这使得调试 IR 变得极其高效。

## 2.5. 下一步

现在你学会了如何构建 IR 并确保其正确性。下一步是学习如何**分析**这些 IR 结构。

**[-\> 下一篇：指南：如何运行分析遍 (Analysis Pass)](03_how_to_run_analysis.md)**

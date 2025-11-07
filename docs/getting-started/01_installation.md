# 1. 构建与测试

`Calico` 是一个专为开发者和编译器研究者设计的 C23 工具套件。因此，它没有传统的“安装”步骤；你将直接从源代码构建、测试和使用它。

本文档将指导你如何设置开发环境并验证构建。

## 1.1. ❗ 环境依赖 (必读)

在开始之前，请**严格**确保你的环境满足以下所有条件。

* **编译器**: **Clang (版本 20 或更高)**
  
  * **[!]** 本项目**强依赖 Clang** 及其特定的 C23 语法实现。
  * `gcc` **不支持**，因为 C23 的某些细节语法在 Clang 和 GCC 之间存在差异。
  * *开发环境验证于: `clang version 20.1.8`*

* **构建系统**:
  
  * `make`

* **辅助工具**:
  
  * `python3`: 用于运行代码质量（格式化、许可证）脚本。
  * `clang-format`: 用于代码格式化，`make test` 会自动调用它进行检查。

## 1.2. 获取源码

使用 `git` 克隆项目仓库：

```bash
git clone [https://github.com/Karesis/calico.git](https://github.com/Karesis/calico.git)
cd calico
```

## 1.3. 核心构建与测试工作流

`Makefile` 已经将代码质量检查（Linting）和单元测试集成到了一个命令中。

### 步骤 1: 运行完整验证

这是在开发过程中最常使用，也是验证环境是否正确的**唯一**命令：

```bash
make test
```

这个命令会按顺序执行以下操作：

1. **代码格式检查**: (调用 `check-format`) 确保所有代码符合 `clang-format` 规范。
2. **许可证头部检查**: (调用 `check-headers`) 确保所有文件都包含 `NOTICE`。
3. **编译源码**: 将 `src/` 下的所有 `.c` 文件编译为对象文件。
4. **打包静态库**: 将所有对象文件归档为 `build/libcalico.a`。
5. **编译测试**: 编译 `tests/` 目录下的所有测试用例。
6. **链接测试**: 将每个测试用例与 `libcalico.a` 链接。
7. **运行测试**: 自动执行所有测试套件。

### 步骤 2: 检查输出

如果一切顺利，你将看到大量的编译日志，最后以类似这样的信息结尾，表明所有测试均已通过：

```
...
Running test suite (build/test_readme_example_interpreter)...
./build/test_readme_example_interpreter
Result of @add(10, 20): 30
All tests completed.
```

如果你看到了 `All tests completed.`，那么恭喜你，你的构建环境已完美配置！

`make test` 完整的成功输出示例:

```
karesis@Celestina:~/Projects/calico$ make test
Checking C formatting...
--- Calico-IR 代码格式化 ---
模式: 检查 (Check-Only)
...
[OK] 所有被处理的文件均符合格式规范。
Checking license headers...
--- Calico-IR 许可证头部检查 ---
模式: 检查 (Check-Only)
...
[OK] 所有被处理的文件均包含许可证头部。
Compiling tests/test_bitset.c...
...
(大量编译日志)
...
Archiving Static Lib (build/libcalico.a)...
...
Linking Test (build/test_bitset)...
Running test suite (build/test_bitset)...
...
[OK] All Bitset: count_slow tests passed.
...
(大量测试日志)
...
Running test suite (build/test_readme_example_interpreter)...
./build/test_readme_example_interpreter
Result of @add(10, 20): 30
All tests completed.
```

### 步骤 3: 如何修复 Linting 错误 (如果 `make test` 失败)

`make test` 命令**包含**检查 (check)，但它**不包含**自动修复。如果它因为格式或许可证头部问题而失败，你需要运行“修复”命令：

* **修复格式问题**:
  
  ```bash
  make format
  ```

* **修复许可证头部问题**:
  
  ```bash
  make headers
  ```

修复后，**重新运行 `make test`** 以确保所有检查和测试都通过。

## 1.4. 其他有用的构建目标

* **`make all`**
  构建库 (`libcalico.a`) 和所有测试可执行文件，但不运行它们。

* **`make lib`**
  仅构建静态库 `build/libcalico.a`。

* **`make run_test_X`**
  构建并**只运行**一个特定的测试套件。这对调试非常有用。
  (例如: `make run_test_parser`)

* **`make clean`**
  删除所有构建产物 (`build/` 目录)。

* **`make help`**
  显示 `Makefile` 中定义的所有主要命令的帮助信息。

-----

## 下一步

你已经成功在本地构建并验证了 `Calico`。现在，让我们开始使用它。

**[-\> 下一篇：教程：解析你的第一个 IR](02_tutorial_parser.md)**

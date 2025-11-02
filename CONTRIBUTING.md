# 贡献指南 (Contributing to Calico-IR)

我们非常欢迎你的贡献！无论是报告一个 Bug、提交一个功能请求，还是直接贡献代码。

作为项目的维护者 (BDFL)，为了保持项目的质量和方向，所有贡献都将由 [Karesis](https://github.com/Karesis) 进行审查。

## 我们的理念

* **代码质量为先：** 提交的代码必须是整洁、可读且经过测试的。
* **保持精简：** 本项目旨在实现一个核心的、受 LLVM 启发的 IR 框架，而非包含所有可能的优化。

## 报告 Bug (Reporting Bugs)

1.  请先在 [Issues 页面](https://github.com/Karesis/calir/issues) (请将 "Karesis/calir" 替换为你的仓库路径) 搜索，确保这个 Bug 没有被重复提交。
2.  创建一个新的 Issue，并使用 "Bug Report" 模板。
3.  请提供：
    * 你做了什么 (What you did)。
    * 你期望发生什么 (What you expected)。
    * 实际发生了什么 (What actually happened)，包括完整的错误日志、出错的 IR 文本等。

## 提交功能请求 (Feature Requests)

1.  在提交大型新功能（例如一个新的优化遍）的 PR 之前，请**务必**先创建一个 Issue (使用 "Feature Request" 模板) 来讨论你的想法。
2.  这可以防止你花费大量时间编写的代码，最后却因为不符合项目的设计哲学而被拒绝。

## 提交代码 (Pull Requests)

1.  **Fork** 本仓库并从 `main` 分支创建你的新分支。
2.  **编码：** 请确保你的代码遵循项目现有的编码风格。
3.  **许可证：** 所有新创建的 `.c` 和 `.h` 文件**必须**包含 Apache 2.0 声明头。
    * 你可以运行 `make headers` 来自动添加它。
4.  **测试 (!! 必须 !!)**
    * 如果你修复了一个 Bug，请在 `tests/` 目录下添加一个新的**回归测试** (regression test) 来验证修复。
    * 如果你添加了一个新功能，请为新功能编写完整的单元测试。
    * 在提交 PR 前，**必须**在本地运行 `make test` 并确保所有测试（包括 `check-headers`）都 100% 通过。
5.  **提交 PR：** 提交一个 Pull Request，清楚地描述你做了什么以及为什么。

感谢你的贡献！
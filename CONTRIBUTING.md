# Contributing to Calico-IR

We warmly welcome your contributions! Whether it's reporting a bug, submitting a feature request, or contributing code directly.

As the project's maintainer (BDFL), all contributions will be reviewed by [Karesis](https://github.com/Karesis) to maintain the project's quality and direction.

## Our Philosophy

* **Code Quality First:** Submitted code must be clean, readable, and well-tested.
* **Stay Lean:** This project aims to be a core, LLVM-inspired IR framework, not to include every possible optimization.

## Reporting Bugs

1.  Please search the [Issues Page](https://github.com/Karesis/calir/issues) (Please replace "Karesis/calir" with your repository path) to ensure the bug hasn't already been reported.
2.  Create a new Issue using the "Bug Report" template.
3.  Please provide:
    * What you did.
    * What you expected to happen.
    * What actually happened (including full error logs, the problematic IR text, etc.).

## Feature Requests

1.  Before submitting a PR for a large new feature (e.g., a new optimization pass), please **be sure** to create an Issue (using the "Feature Request" template) to discuss your idea first.
2.  This prevents you from spending a lot of time on code that might be rejected later because it doesn't align with the project's design philosophy.

## Pull Requests

1.  **Fork** this repository and create your new branch from `main`.
2.  **Code Style:** Your code **must** follow our **[Code Style Guide (STYLE.MD)](STYLE.MD)**.
3.  **Formatting:** Before submitting, please **must** run `make format` to automatically format your code.
4.  **License:** All new `.c` and `.h` files **must** include the Apache 2.0 license header.
    * You can run `make headers` to add it automatically.
5.  **Tests (!! Required !!)**
    * If you fixed a bug, please add a new **regression test** in the `tests/` directory to verify the fix.
    * If you added a new feature, please write comprehensive unit tests for it.
    * Before submitting your PR, you **must** run `make test` locally and ensure all tests (including `check-headers` and `check-format`) pass 100%.
6.  **Submit PR:** Submit a Pull Request, clearly describing what you did and why.

Thank you for your contribution!
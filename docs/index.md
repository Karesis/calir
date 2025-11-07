# Welcome to the Calico Documentation

This is the official documentation site for Calico, a lightweight, LLVM-inspired compiler toolkit written in pure C23.

This site contains hands-on tutorials, deep-dive guides, and architectural concepts to help you use, understand, and extend the project.

---

## How to Use These Docs

This documentation is split into four main sections, designed for different goals.

### 📖 Getting Started
> **Start here.** This section is a hands-on tutorial that will guide you from cloning the project to building it, parsing your first IR, and running it with the interpreter.

### 🔧 How-to Guides
> **Task-oriented "recipes."** This section provides practical, copy-paste examples for common tasks, like building IR with the `IRBuilder`, running the `mem2reg` pass, or verifying IR correctness.

### 🧠 Core Concepts
> **The "theory" and "why."** Once you know *how* to use Calico, this section explains the high-level architecture, the `calir` text format, and the design philosophy behind `IRValue` and the Use-Def chain.

### reference/
> **The API dictionary.** (Coming Soon) This section will contain the complete, auto-generated API reference for every public function in the `include/` directory.

---

## Let's Begin

If you are new to Calico, the best place to start is the first tutorial in the "Getting Started" series.

**[-> Start the Tutorial: 1. Building and Testing](getting-started/01_build_and_test.md)**
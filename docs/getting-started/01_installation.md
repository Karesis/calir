# 1. Building and Testing

`Calico` is a C23 toolkit designed for developers and compiler researchers. As such, it doesn't have a traditional "installation" step; you will build, test, and use it directly from the source code.

This document will guide you through setting up your environment and verifying the build.

## 1.1. ❗ Environment Dependencies (Required)

Before you begin, please **strictly** ensure your environment meets all of the following conditions.

* **Compiler**: **Clang (Version 20 or higher)**
  
  * **[!]** This project has a **strong dependency on Clang** and its specific C23 syntax implementation.
  * `gcc` is **not supported**, as some C23 syntax details differ between Clang and GCC.
  * *Development environment verified on: `clang version 20.1.8`*

* **Build System**:
  
  * `make`

* **Helper Tools**:
  
  * `python3`: Required for running code quality (formatting, license) scripts.
  * `clang-format`: Required for code formatting. `make test` will automatically invoke it for checking.

## 1.2. Get the Source

First, clone the project repository using `git`:

```bash
git clone https://github.com/Karesis/calico.git
cd calico
````

## 1.3. Core Build & Test Workflow

The `Makefile` has integrated code quality checks (Linting) and unit tests into a single command.

### Step 1: Run Full Verification

This is the **only command** you need to verify your environment is configured correctly and is the one you will use most often during development:

```bash
make test
```

This command will execute the following steps in order:

1.  **Code Format Check**: (Calls `check-format`) Ensures all code conforms to the `clang-format` specification.
2.  **License Header Check**: (Calls `check-headers`) Ensures all files contain the `NOTICE`.
3.  **Compile Source**: Compiles all `.c` files under `src/` into object files.
4.  **Archive Static Library**: Archives all object files into `build/libcalico.a`.
5.  **Compile Tests**: Compiles all test cases under `tests/`.
6.  **Link Tests**: Links each test case against `libcalico.a`.
7.  **Run Tests**: Automatically executes all test suites.

### Step 2: Check the Output

If everything is successful, you will see a large amount of compilation logs, ending with a message similar to this, indicating all tests have passed:

```
...
Running test suite (build/test_readme_example_interpreter)...
./build/test_readme_example_interpreter
Result of @add(10, 20): 30
All tests completed.
```

If you see `All tests completed.`, congratulations, your build environment is perfectly configured!

```
karesis@Celestina:~/Projects/calico$ make test
Checking C formatting...
--- Calico-IR Code Formatting ---
Mode: Check-Only
...
[OK] All processed files conform to formatting standards.
Checking license headers...
--- Calico-IR License Header Check ---
Mode: Check-Only
...
[OK] All processed files contain a license header.
Compiling tests/test_bitset.c...
...
(Extensive compilation log)
...
Archiving Static Lib (build/libcalico.a)...
...
Linking Test (build/test_bitset)...
Running test suite (build/test_bitset)...
...
[OK] All Bitset: count_slow tests passed.
...
(Extensive test log)
...
Running test suite (build/test_readme_example_interpreter)...
./build/test_readme_example_interpreter
Result of @add(10, 20): 30
All tests completed.
```

### Step 3: How to Fix Linting Errors (If `make test` fails)

The `make test` command **includes** checks, but it does **not** include automatic fixes. If it fails due to formatting or license header issues, you need to run the "fix" commands:

  * **To fix formatting issues**:

    ```bash
    make format
    ```

  * **To fix license header issues**:

    ```bash
    make headers
    ```

After fixing, **re-run `make test`** to ensure all checks and tests pass.

## 1.4. Other Useful Build Targets

  * **`make all`**
    Builds the library (`libcalico.a`) and all test executables, but does not run them.

  * **`make lib`**
    Builds only the static library `build/libcalico.a`.

  * **`make run_test_X`**
    Builds and **only runs** a single, specific test suite. This is very useful for debugging.
    (e.g., `make run_test_parser`)

  * **`make clean`**
    Removes all build artifacts (`build/` directory).

  * **`make help`**
    Displays help information for all primary commands defined in the `Makefile`.

-----

## Next Steps

You have successfully built and verified `Calico` locally. Now, let's start using its core features.

**[-\> Next: Tutorial: Parsing Your First IR](02_tutorial_parser.md)**


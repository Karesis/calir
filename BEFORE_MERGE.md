1. test
make test            - Build and run ALL test suites (alias: 'make run').

2. linting
make format          - Auto-format all .c/.h files with clang-format.
make check-format    - Check if all files are formatted (CI mode).
make headers         - Apply missing license headers.
make check-headers   - Check for missing license headers (CI mode).
make clean-comments  - Remove temporary '//' comments from code.

3. test again
make test            - Build and run ALL test suites (alias: 'make run').

4. retry all if failed at any point
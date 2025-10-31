# =================================================================
# --- 1. 项目配置 (Configuration) ---
# =================================================================

# 编译器
CC = clang
# 归档工具 (用于创建静态库)
AR = ar

# --- 构建目标 ---
LIB_NAME = libcalir.a

# 目录
BUILD_DIR = build
OBJ_DIR = $(BUILD_DIR)/obj

# 目标全路径
LIB_TARGET = $(BUILD_DIR)/$(LIB_NAME)

# =================================================================
# --- 2. 编译和链接标志 (Flags) ---
# =================================================================

# C 标志: std=c23, 警告, 调试信息, 自动依赖
CFLAGS_BASE = -std=c23 -Wall -Wextra -g -O0 -MMD -MP

# 头文件包含路径 (-I)
CPPFLAGS = -Iinclude

# 链接库
LDLIBS = -lm

# 链接标志 (链接我们自己的库)
LDFLAGS = -L$(BUILD_DIR)

# --- 特定于文件的 CFLAGS ---
CFLAGS_HASHMAP = -mavx2
ifeq ($(OS),Windows_NT)
  CFLAGS_BUMP =
else
  CFLAGS_BUMP = -D_POSIX_C_SOURCE=200809L
endif

# --- 组合通用 CFLAGS ---
CFLAGS_COMMON = $(CFLAGS_BASE) $(CPPFLAGS)

# =================================================================
# --- 3. 文件发现 (File Discovery) ---
# =================================================================

# --- 库文件 (你的项目核心) ---
UTILS_SRCS = $(wildcard src/utils/*.c) \
             $(wildcard src/utils/hashmap/*.c)
IR_SRCS = $(wildcard src/ir/*.c)
ANALYSIS_SRCS = $(wildcard src/analysis/*.c)
TRANSFORM_SRCS = $(wildcard src/transforms/*.c)
INTERPRETER_SRCS = $(wildcard src/interpreter/*.c)

LIB_SRCS = $(UTILS_SRCS) $(IR_SRCS) $(ANALYSIS_SRCS) $(TRANSFORM_SRCS) $(INTERPRETER_SRCS)
LIB_OBJS = $(patsubst src/%.c, $(OBJ_DIR)/%.o, $(LIB_SRCS))

# --- 自动化测试发现 ---
TEST_SRCS = $(wildcard tests/test_*.c)
TEST_OBJS = $(patsubst tests/%.c, $(OBJ_DIR)/tests/%.o, $(TEST_SRCS))
TEST_TARGETS = $(patsubst tests/%.c, $(BUILD_DIR)/%, $(TEST_SRCS))
TEST_RUNNERS = $(patsubst tests/test_%.c, run_test_%, $(TEST_SRCS))

# --- 自动依赖文件 ---
# [!!] 移除了 $(MAIN_OBJ)
ALL_OBJS = $(LIB_OBJS) $(TEST_OBJS)
DEPS = $(ALL_OBJS:.o=.d)

# --- 用于特定 CFLAGS 的对象集 ---
BUMP_OBJ = $(OBJ_DIR)/utils/bump.o
HASHMAP_OBJS = $(filter $(OBJ_DIR)/utils/hashmap/%.o, $(LIB_OBJS))

# =================================================================
# --- 4. 主要规则 (Main Rules) ---
# =================================================================

# [!!] 默认目标: 构建核心库和所有测试
.PHONY: all
all: $(LIB_TARGET) build_tests

# 显式构建所有测试可执行文件
.PHONY: build_tests
build_tests: $(TEST_TARGETS)

# 运行所有测试 (会先构建)
.PHONY: test
test: $(TEST_RUNNERS)
	@echo "\nAll tests completed."

# --- 静态库规则 ---
$(LIB_TARGET): $(LIB_OBJS)
	@echo "Archiving Static Lib ($@)..."
	@mkdir -p $(@D)
	$(AR) rcs $@ $^

# --- [!!] 移除了链接主目标的规则 ---

# --- 自动化测试链接规则 ---
$(TEST_TARGETS): $(BUILD_DIR)/%: $(OBJ_DIR)/tests/%.o $(LIB_TARGET)
	@echo "Linking Test ($@)..."
	@mkdir -p $(@D)
	$(CC) $(LDFLAGS) -o $@ $< -lcalir $(LDLIBS)

# =================================================================
# --- 5. 编译规则 (Compilation Rules) ---
# =================================================================

# --- 目标特定的 CFLAGS ---
$(ALL_OBJS): CFLAGS = $(CFLAGS_COMMON)
$(BUMP_OBJ): CFLAGS = $(CFLAGS_COMMON) $(CFLAGS_BUMP)
$(HASHMAP_OBJS): CFLAGS = $(CFLAGS_COMMON) $(CFLAGS_HASHMAP)

# --- 通用编译规则 (src/) ---
$(OBJ_DIR)/%.o: src/%.c
	@mkdir -p $(@D)
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) -c $< -o $@

# --- 通用编译规则 (tests/) ---
$(OBJ_DIR)/tests/%.o: tests/%.c
	@mkdir -p $(@D)
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) -c $< -o $@

# =================================================================
# --- 6. 清理和运行 (Utility Rules) ---
# =================================================================

# [!!] 'help' 目标已更新
.PHONY: help
help:
	@echo "Available commands:"
	@echo "  make (all)           - Build the main library (libcalir.a) and all test binaries."
	@echo "  make lib             - Build only the static library (libcalir.a)."
	@echo "  make build_tests     - Build ALL test executables in tests/."
	@echo "  make test            - Run ALL test suites (e.g., test_bitset, test_hashmap)."
	@echo "  make run             - Alias for 'make test'. Runs ALL test suites."
	@echo "  make re              - Clean and rebuild 'all'."
	@echo "  make clean           - Remove all build artifacts."
	@echo "  --- Individual Tests (for development) ---"
	@echo "  make build/test_X    - Build only a *single* test (e.g., make build/test_bitset)."
	@echo "  make run_test_X      - Build and run a *single* test (e.g., make run_test_bitset)."

# 只构建库的快捷方式
.PHONY: lib
lib: $(LIB_TARGET)

# 清理所有构建产物
.PHONY: clean
clean:
	@echo "Cleaning build files..."
	rm -rf $(BUILD_DIR)

# 重新构建 (清理 + 构建)
.PHONY: re
re: clean all

# [!!] 'run' 目标现在是 'test' 的别名
.PHONY: run
run: test

# 自动化运行规则 
.PHONY: $(TEST_RUNNERS)

# 模式规则: 'make run_test_bitset'
$(TEST_RUNNERS): run_test_%: $(BUILD_DIR)/test_%
	@echo "Running test suite ($<)..."
	./$<

# =================================================================
# --- 7. 包含自动依赖 ---
# =================================================================
-include $(DEPS)
# =================================================================
# --- 1. 项目配置 (Configuration) ---
# =================================================================

# 编译器
CC = clang
# CC = gcc

# --- 构建目标 ---
# 默认主目标 (calir IR 测试)
TARGET_CALIR_NAME = calir_test
TARGET_CALIR = $(BUILD_DIR)/$(TARGET_CALIR_NAME)

# 目录
BUILD_DIR = build
OBJ_DIR = $(BUILD_DIR)/obj

# =================================================================
# --- 2. 编译和链接标志 (Flags) ---
# =================================================================

# C 标志: std=c23, 警告, 调试信息
CFLAGS_BASE = -std=c23 -Wall -Wextra -g -O0

# 头文件包含路径 (-I)
CPPFLAGS = -Iinclude

# 链接库 (保留 -lm 以防万一)
LDLIBS = -lm

# 链接标志 (例如 -L)
LDFLAGS =

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

# --- 库文件 ---
UTILS_SRCS = $(wildcard src/utils/*.c) \
             $(wildcard src/utils/hashmap/*.c)
IR_SRCS = $(wildcard src/ir/*.c)
ANALYSIS_SRCS = $(wildcard src/analysis/*.c)
TRANSFORM_SRCS = $(wildcard src/transforms/*.c)

# --- 主程序文件 ---
MAIN_CALIR_SRC = src/main_test.c

# --- 自动化测试发现 ---
# 1. 找到所有 test_*.c 源文件
TEST_SRCS = $(wildcard tests/test_*.c)

# 2. 将 .c 映射到 .o (对象文件)
# e.g., tests/test_bitset.c -> build/obj/tests/test_bitset.o
TEST_OBJS = $(patsubst tests/%.c, $(OBJ_DIR)/tests/%.o, $(TEST_SRCS))

# 3. 将 .c 映射到可执行目标
# e.g., tests/test_bitset.c -> build/test_bitset
TEST_TARGETS = $(patsubst tests/%.c, $(BUILD_DIR)/%, $(TEST_SRCS))

# 4. 创建 "run_test_*" 伪目标名称
# e.g., tests/test_bitset.c -> run_test_bitset
TEST_RUNNERS = $(patsubst tests/test_%.c, run_test_%, $(TEST_SRCS))

# --- 将所有 .c 映射到 .o ---
UTILS_OBJS = $(patsubst src/%.c, $(OBJ_DIR)/%.o, $(UTILS_SRCS))
IR_OBJS = $(patsubst src/%.c, $(OBJ_DIR)/%.o, $(IR_SRCS))
ANALYSIS_OBJS = $(patsubst src/%.c, $(OBJ_DIR)/%.o, $(ANALYSIS_SRCS))
TRANSFORM_OBJS = $(patsubst src/%.c, $(OBJ_DIR)/%.o, $(TRANSFORM_SRCS))
MAIN_CALIR_OBJ = $(patsubst src/%.c, $(OBJ_DIR)/%.o, $(MAIN_CALIR_SRC))

# --- 库对象 (所有测试都将链接它们) ---
LIB_OBJS = $(UTILS_OBJS) $(IR_OBJS) $(ANALYSIS_OBJS) $(TRANSFORM_OBJS)

# --- 用于特定 CFLAGS 的对象集 ---
BUMP_OBJ = $(OBJ_DIR)/utils/bump.o
HASHMAP_OBJS = $(filter $(OBJ_DIR)/utils/hashmap/%.o, $(UTILS_OBJS))

# =================================================================
# --- 4. 主要规则 (Main Rules) ---
# =================================================================

# 默认目标 (e.g., "make" 或 "make all")
.PHONY: all
all: $(TARGET_CALIR)

# [新] "make test" 将构建所有测试
.PHONY: test
test: $(TEST_TARGETS)

# 链接 Calir IR 测试
$(TARGET_CALIR): $(UTILS_OBJS) $(IR_OBJS) $(ANALYSIS_OBJS) $(MAIN_CALIR_OBJ)
	@echo "Linking Calir Test ($@)..."
	@mkdir -p $(@D)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

# 自动化测试链接规则 
# 这是一个静态模式规则
# 它告诉 make 如何构建 *任何* 匹配 `$(TEST_TARGETS)` 列表的目标
#
# 依赖关系:
# $@ (目标)     : build/test_bitset
# $* (词干)     : test_bitset
# $< (第一个依赖): $(OBJ_DIR)/tests/test_bitset.o
# $^ (所有依赖): $(OBJ_DIR)/tests/test_bitset.o $(LIB_OBJS)
#
# 注意: 我们链接 *所有* 库对象 (LIB_OBJS)。
# 这比为每个测试单独指定依赖更简单。
$(TEST_TARGETS): $(BUILD_DIR)/%: $(OBJ_DIR)/tests/%.o $(LIB_OBJS)
	@echo "Linking Test ($@)..."
	@mkdir -p $(@D)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

# =================================================================
# --- 5. 编译规则 (Compilation Rules) ---
# =================================================================

# --- 目标特定的 CFLAGS ---
# (将 MAIN_HASHMAP_OBJ 替换为 TEST_OBJS)
ALL_OBJS = $(UTILS_OBJS) $(IR_OBJS) $(ANALYSIS_OBJS) $(TRANSFORM_OBJS) $(MAIN_CALIR_OBJ) $(TEST_OBJS)

# 默认情况下，所有对象都使用通用 CFLAGS
$(ALL_OBJS): CFLAGS = $(CFLAGS_COMMON)

# *覆盖* bump.o 的 CFLAGS
$(BUMP_OBJ): CFLAGS = $(CFLAGS_COMMON) $(CFLAGS_BUMP)

# *覆盖*所有 hashmap/*.o 的 CFLAGS
$(HASHMAP_OBJS): CFLAGS = $(CFLAGS_COMMON) $(CFLAGS_HASHMAP)


# --- 通用编译规则 ---
# (这些规则无需更改，它们已经很完美)

# 规则 1: 编译 src/ 目录下的文件
$(OBJ_DIR)/%.o: src/%.c
	@mkdir -p $(@D)
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) -c $< -o $@

# 规则 2: 编译 tests/ 目录下的文件
# (这将自动处理所有 test_*.c 文件)
$(OBJ_DIR)/tests/%.o: tests/%.c
	@mkdir -p $(@D)
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) -c $< -o $@

# =================================================================
# --- 6. 清理和运行 (Utility Rules) ---
# =================================================================

# 清理所有构建产物
.PHONY: clean
clean:
	@echo "Cleaning build files..."
	rm -rf $(BUILD_DIR)

# 重新构建 (清理 + 构建)
.PHONY: re
re: clean all

# 运行 *默认* (Calir) 测试
.PHONY: run
run: all
	@echo "Running Calir test suite..."
	./$(TARGET_CALIR)

# 自动化运行规则 

# 定义所有 "run_test_*" 目标为伪目标
.PHONY: $(TEST_RUNNERS) run_all_tests

# 这是一个模式规则，用于创建 run_test_* 目标
# 例如: 'make run_test_bitset'
# 1. 它会匹配 'run_test_%' (e.g., run_test_bitset)
# 2. 它依赖于 '$(BUILD_DIR)/test_%' (e.g., build/test_bitset)
# 3. 它运行依赖的可执行文件
$(TEST_RUNNERS): run_test_%: $(BUILD_DIR)/test_%
	@echo "Running test suite ($<)..."
	./$<

# "make run_all_tests" 将运行 *所有* 测试
run_all_tests: $(TEST_RUNNERS)
	@echo "\nAll tests completed."


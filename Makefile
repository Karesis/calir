# =================================================================
# --- 1. 项目配置 (Configuration) ---
# =================================================================

# 编译器
CC = clang
# CC = gcc

# 最终可执行文件名 (已更新为新的测试)
TARGET_NAME = test_hashmap

# 目录
BUILD_DIR = build
OBJ_DIR = $(BUILD_DIR)/obj
TARGET = $(BUILD_DIR)/$(TARGET_NAME)

# =================================================================
# --- 2. 编译和链接标志 (Flags) ---
# =================================================================

# C 标志: std=c23, 警告, 调试信息
CFLAGS_BASE = -std=c23 -Wall -Wextra -g

# 头文件包含路径 (-I)
CPPFLAGS = -Iinclude

# 链接库 (test_hashmap.c 需要 -lm)
LDLIBS = -lm

# 链接标志 (例如 -L)
LDFLAGS =

# --- 特定于文件的 CFLAGS ---

# 1. hashmap: 需要 AVX2 (所有 hashmap/*.c 都使用)
CFLAGS_HASHMAP = -mavx2

# 2. bump.c: 需要 POSIX.1-2008 (非 Windows)
ifeq ($(OS),Windows_NT)
  CFLAGS_BUMP =
else
  CFLAGS_BUMP = -D_POSIX_C_SOURCE=200809L
endif

# --- 组合通用 CFLAGS ---
CFLAGS_COMMON = $(CFLAGS_BASE) $(CPPFLAGS)

# =================================================================
# --- 3. 文件发现 (File Discovery for test_hashmap) ---
# =================================================================

# --- [已更新] ---
# 我们不再编译 src/ir/* 或 src/lib.c
# 我们只指定 test_hashmap 明确需要的 'utils' 库

# 1. 'utils' 库的源文件
#    (wildcard 会自动找到新的 generic.c)
UTILS_SRCS = $(wildcard src/utils/*.c) \
             $(wildcard src/utils/hashmap/*.c)

# 2. 'test_hashmap' 的主源文件
TEST_MAIN_SRC = tests/test_hashmap.c

# 3. 将所有源文件映射到 .o 对象文件
UTILS_OBJS = $(patsubst src/%.c, $(OBJ_DIR)/%.o, $(UTILS_SRCS))
TEST_MAIN_OBJ = $(patsubst tests/%.c, $(OBJ_DIR)/tests/%.o, $(TEST_MAIN_SRC))

# 4. 最终链接所需的所有 .o 文件
ALL_OBJS = $(UTILS_OBJS) $(TEST_MAIN_OBJ)

# 5. 找出特定规则的对象 (用于 CFLAGS 覆盖)
BUMP_OBJ = $(OBJ_DIR)/utils/bump.o
HASHMAP_OBJS = $(filter $(OBJ_DIR)/utils/hashmap/%.o, $(ALL_OBJS))

# =================================================================
# --- 4. 主要规则 (Main Rules) ---
# =================================================================

# 默认目标 (e.g., "make" 或 "make all")
.PHONY: all
all: $(TARGET)

# 链接可执行文件
$(TARGET): $(ALL_OBJS)
	@echo "Linking $@..."
	@mkdir -p $(@D) # 确保 build/ 目录存在
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

# =================================================================
# --- 5. 编译规则 (Compilation Rules) ---
# =================================================================

# --- 目标特定的 CFLAGS ---
# 默认情况下，所有对象都使用通用 CFLAGS
$(ALL_OBJS): CFLAGS = $(CFLAGS_COMMON)

# *覆盖* bump.o 的 CFLAGS
$(BUMP_OBJ): CFLAGS = $(CFLAGS_COMMON) $(CFLAGS_BUMP)

# *覆盖*所有 hashmap/*.o 的 CFLAGS
$(HASHMAP_OBJS): CFLAGS = $(CFLAGS_COMMON) $(CFLAGS_HASHMAP)


# --- 通用编译规则 ---

# 规则 1: 编译 src/ 目录下的文件
$(OBJ_DIR)/%.o: src/%.c
	@mkdir -p $(@D)
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) -c $< -o $@

# 规则 2: [新增] 编译 tests/ 目录下的文件
$(OBJ_DIR)/tests/%.o: tests/%.c
	@mkdir -p $(@D)
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) -c $< -o $@

# =================================================================
# --- 6. 清理规则 (Utility Rules) ---
# =================================================================

# 清理所有构建产物
.PHONY: clean
clean:
	@echo "Cleaning build files..."
	rm -rf $(BUILD_DIR)

# 重新构建 (清理 + 构建)
.PHONY: re
re: clean all

# [新增] 运行测试
.PHONY: run
run: all
	@echo "Running test suite..."
	./$(TARGET)

# =================================================================
# --- 1. 项目配置 (Configuration) ---
# =================================================================

# 编译器
CC = clang
# CC = gcc

# 最终可执行文件名
TARGET_NAME = calir_test

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

# 链接库 (例如 -lm for math)
LDLIBS = -lm

# 链接标志 (例如 -L)
LDFLAGS =

# --- 特定于文件的 CFLAGS ---

# 1. hashmap: 需要 AVX2
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
# --- 3. 文件发现 (File Discovery) ---
# =================================================================

# 自动查找所有 .c 源文件
ALL_SRCS = $(shell find src -name "*.c")

# 将所有 src/%.c 路径 映射到 build/obj/%.o 路径
ALL_OBJS = $(patsubst src/%.c, $(OBJ_DIR)/%.o, $(ALL_SRCS))

# 找出特定规则的对象
# 1. bump.c 的对象
BUMP_OBJ = $(OBJ_DIR)/utils/bump.o
# 2. 所有 hashmap 的对象
HASHMAP_OBJS = $(patsubst src/utils/hashmap/%.c, $(OBJ_DIR)/utils/hashmap/%.o, $(wildcard src/utils/hashmap/*.c))

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
#
# 这是一个单一的模式规则，用于将 *任何* src/%.c 编译到
# 对应的 obj/%.o，同时自动创建其目录。
# 它将使用上面设置的 "目标特定 CFLAGS" 变量。
#
$(OBJ_DIR)/%.o: src/%.c
	@mkdir -p $(@D) # 确保 build/obj/ir 等目录存在
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

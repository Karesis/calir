# =================================================================
# --- 1. 项目配置 (Configuration) ---
# =================================================================

# 编译器
CC = clang
# CC = gcc

# --- [新] 构建目标 ---
# 默认目标 (calir IR 测试)
TARGET_CALIR_NAME = calir_test
TARGET_CALIR = $(BUILD_DIR)/$(TARGET_CALIR_NAME)

# 可选目标 (hashmap 单元测试)
TARGET_HASHMAP_NAME = test_hashmap
TARGET_HASHMAP = $(BUILD_DIR)/$(TARGET_HASHMAP_NAME)

# 目录
BUILD_DIR = build
OBJ_DIR = $(BUILD_DIR)/obj

# =================================================================
# --- 2. 编译和链接标志 (Flags) ---
# =================================================================

# C 标志: std=c23, 警告, 调试信息
# (使用 -g -O0 来确保 assert() 启用并获得最佳调试体验)
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
# (自动找到新的 generic.c)
UTILS_SRCS = $(wildcard src/utils/*.c) \
             $(wildcard src/utils/hashmap/*.c)
IR_SRCS = $(wildcard src/ir/*.c)

# --- 主程序文件 ---
MAIN_CALIR_SRC = src/main_test.c
MAIN_HASHMAP_SRC = tests/test_hashmap.c

# --- 将所有 .c 映射到 .o ---
UTILS_OBJS = $(patsubst src/%.c, $(OBJ_DIR)/%.o, $(UTILS_SRCS))
IR_OBJS = $(patsubst src/%.c, $(OBJ_DIR)/%.o, $(IR_SRCS))

MAIN_CALIR_OBJ = $(patsubst src/%.c, $(OBJ_DIR)/%.o, $(MAIN_CALIR_SRC))
MAIN_HASHMAP_OBJ = $(patsubst tests/%.c, $(OBJ_DIR)/tests/%.o, $(MAIN_HASHMAP_SRC))

# --- 用于特定 CFLAGS 的对象集 ---
BUMP_OBJ = $(OBJ_DIR)/utils/bump.o
HASHMAP_OBJS = $(filter $(OBJ_DIR)/utils/hashmap/%.o, $(UTILS_OBJS))

# =================================================================
# --- 4. 主要规则 (Main Rules) ---
# =================================================================

# 默认目标 (e.g., "make" 或 "make all")
.PHONY: all
all: $(TARGET_CALIR)

# [新] 链接 Calir IR 测试
$(TARGET_CALIR): $(UTILS_OBJS) $(IR_OBJS) $(MAIN_CALIR_OBJ)
	@echo "Linking Calir Test ($@)..."
	@mkdir -p $(@D)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

# [新] 链接 Hashmap 单元测试 (可选)
.PHONY: test_hashmap
test_hashmap: $(TARGET_HASHMAP)

$(TARGET_HASHMAP): $(UTILS_OBJS) $(MAIN_HASHMAP_OBJ)
	@echo "Linking Hashmap Test ($@)..."
	@mkdir -p $(@D)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

# =================================================================
# --- 5. 编译规则 (Compilation Rules) ---
# =================================================================

# --- 目标特定的 CFLAGS ---
# (我们收集所有可能的 .o 文件)
ALL_OBJS = $(UTILS_OBJS) $(IR_OBJS) $(MAIN_CALIR_OBJ) $(MAIN_HASHMAP_OBJ)

# 默认情况下，所有对象都使用通用 CFLAGS
$(ALL_OBJS): CFLAGS = $(CFLAGS_COMMON)

# *覆盖* bump.o 的 CFLAGS
$(BUMP_OBJ): CFLAGS = $(CFLAGS_COMMON) $(CFLAGS_BUMP)

# *覆盖*所有 hashmap/*.o 的 CFLAGS
$(HASHMAP_OBJS): CFLAGS = $(CFLAGS_COMMON) $(CFLAGS_HASHMAP)


# --- 通用编译规则 ---

# 规则 1: 编译 src/ 目录下的文件
# (这会处理 utils, hashmap, ir, 和 main_test.c)
$(OBJ_DIR)/%.o: src/%.c
	@mkdir -p $(@D)
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) -c $< -o $@

# 规则 2: 编译 tests/ 目录下的文件
# (这会处理 test_hashmap.c)
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

# [新] 运行 *hashmap* 测试
.PHONY: run_hashmap
run_hashmap: test_hashmap
	@echo "Running Hashmap test suite..."
	./$(TARGET_HASHMAP)
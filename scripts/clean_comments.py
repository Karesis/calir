#!/usr/bin/env python3
import sys
from pathlib import Path

# -----------------------------------------------------------------
# [!!] 配置: 从 'apply_license.py' 复制而来
# -----------------------------------------------------------------

# 要扫描的文件夹
DIRS_TO_SCAN = ["src", "include", "tests"]

# 目标文件扩展名
TARGET_EXTS = {".c", ".h"}

# -----------------------------------------------------------------
# [!!] 要排除的第三方路径 (与 apply_license.py 保持一致)
# -----------------------------------------------------------------
THIRD_PARTY_PATHS = {
    # xxHash
    "include/utils/xxhash.h",
    "src/utils/xxhash.c",
}

def process_file(file_path: Path) -> int:
    """
    处理单个文件，移除 '临时' 注释 (包括行内)。
    - 'temp' 注释定义为: '//' (非 '///'), 且不在字符串、
      字符或块注释内部。
    - 返回文件是否被修改 (1 表示修改, 0 表示未修改)。
    """
    try:
        content = file_path.read_text(encoding="utf-8")
    except (IOError, UnicodeDecodeError) as e:
        print(f"  [ERROR] 无法读取 {file_path}: {e}", file=sys.stderr)
        return 0 # 跳过

    new_lines = []
    original_lines = content.splitlines()
    file_changed_flag = False # 标记文件内容是否被修改
    
    # 词法分析器的状态 (跨行保持)
    in_block_comment = False
    
    for line in original_lines:
        new_line = "" # 重新构建的行
        
        # 状态 (行内重置)
        in_string = False # "..."
        in_char = False   # '...'
        
        i = 0
        while i < len(line):
            # --- 1. 优先处理多行块注释的状态 ---
            if in_block_comment:
                if i + 1 < len(line) and line[i:i+2] == "*/":
                    # 块注释结束
                    in_block_comment = False
                    new_line += "*/" # <--- [!!] 修复: 保留 "*/"
                    i += 2 # 跳过 "*/"
                else:
                    # 仍在块注释中，保留字符
                    new_line += line[i]
                    i += 1
                continue # 处理下一个字符

            # --- 2. 检查转义字符 ---
            # (这能正确处理 " \" " 和 ' \' ' 以及 " \\" ")
            is_escaped = False
            if i > 0 and line[i-1] == '\\':
                j = i - 1
                slash_count = 0
                while j >= 0 and line[j] == '\\':
                    slash_count += 1
                    j -= 1
                if slash_count % 2 == 1: # 奇数个反斜杠表示转义
                    is_escaped = True

            # --- 3. 检查是否进入字符串或字符字面量 (非块注释状态下) ---
            if not in_char and line[i] == '"' and not is_escaped:
                in_string = not in_string
            
            if not in_string and line[i] == "'" and not is_escaped:
                in_char = not in_char

            # --- 4. 检查是否进入注释 (仅当不在字符串或字符中时) ---
            if not in_string and not in_char:
                # 检查是否进入块注释
                if i + 1 < len(line) and line[i:i+2] == "/*":
                    in_block_comment = True
                    new_line += "/*"
                    i += 2
                    continue
                
                # 检查是否进入行注释
                if i + 1 < len(line) and line[i:i+2] == "//":
                    if i + 2 < len(line) and line[i+2] == "/":
                        # 是 '///' (文档注释), 保留
                        new_line += line[i:] # 保留从 '///' 开始的剩余所有内容
                        break # 处理下一行
                    else:
                        # 是 '//' (临时注释), 停止处理
                        # 我们不保留这部分内容，直接中断循环
                        file_changed_flag = True # 标记发生了变化
                        break # 处理下一行
            
            # --- 5. 默认：保留当前字符 ---
            new_line += line[i]
            i += 1
            
        # 循环结束后，我们将新的（可能被截断的）行加入列表
        # (我们截断了 '//' 后面的内容, 自动删除了尾随空格)
        new_lines.append(new_line.rstrip()) # rstrip() 清理行尾空格

    # --- 文件处理完毕 ---
    
    new_content = "\n".join(new_lines)
    
    # 保持原始文件末尾的换行符一致性
    if content.endswith('\n'):
        if not new_content:
            # 如果原始文件有内容且有换行符，现在空了，保留换行符
            new_content = "\n"
        elif not new_content.endswith('\n'):
            # 如果新内容非空，确保它有换行符
            new_content += "\n"
    elif not content and not new_content:
        # 原始文件是空的，新文件也是空的，不变
        pass
    elif new_content and content.endswith('\n'):
         # 确保新内容有换行符
         pass
    
    # 只有当内容真的发生变化时才写入
    if new_content != content:
        print(f"  [CLEANED] {file_path}")
        try:
            file_path.write_text(new_content, encoding="utf-8")
            return 1 # 报告文件已被更改
        except IOError as e:
            print(f"  [ERROR] 无法写入 {file_path}: {e}", file=sys.stderr)
            return 0
    
    return 0 # 未更改

# -----------------------------------------------------------------
# [!!] 以下函数 (is_excluded, main) 保持不变
# -----------------------------------------------------------------
def is_excluded(relative_path_str: str) -> bool:
    """
    检查文件路径是否在排除列表中。
    (与 apply_license.py 相同)
    """
    for exclusion_prefix in THIRD_PARTY_PATHS:
        if relative_path_str.startswith(exclusion_prefix):
            return True
    return False

def main():
    project_root = Path(__file__).parent.parent
    print("--- Calico-IR 临时注释清理 ---")
    print("模式: 清理 (Clean)")

    total_files_processed = 0
    total_files_skipped = 0
    total_files_changed = 0

    for dir_name in DIRS_TO_SCAN:
        search_dir = project_root / dir_name
        if not search_dir.is_dir():
            print(f"\n[WARN] 目录 '{dir_name}' 不存在, 跳过。")
            continue
            
        print(f"\nScanning {search_dir}...")
        
        files_in_dir_processed = 0
        
        for ext in TARGET_EXTS:
            for file_path in search_dir.rglob(f"*{ext}"):
                relative_path_str = file_path.relative_to(project_root).as_posix()
                
                if is_excluded(relative_path_str):
                    total_files_skipped += 1
                    continue

                total_files_processed += 1
                files_in_dir_processed += 1
                if process_file(file_path):
                    total_files_changed += 1
        
        if files_in_dir_processed == 0:
            print("  (未找到需要处理的目标文件)")

    print("\n--- 清理完成 ---")
    print(f"总共处理文件: {total_files_processed}")
    print(f"总共跳过文件: {total_files_skipped} (第三方, e.g., xxhash)")
    print(f"总共修改文件: {total_files_changed}")
    
    sys.exit(0)

if __name__ == "__main__":
    main()
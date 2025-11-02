#!/usr/bin/env python3
import sys
import subprocess
from pathlib import Path
import os

# -----------------------------------------------------------------
# [!!] 配置: 与你其他脚本保持一致
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

# -----------------------------------------------------------------
# 核心功能
# -----------------------------------------------------------------

CLANG_FORMAT_BIN = os.environ.get("CLANG_FORMAT_BIN", "clang-format")

def process_file(file_path: Path, check_mode: bool) -> bool:
    """
    对单个文件运行 clang-format。
    - check_mode=True (检查模式):
      运行 'clang-format --dry-run -Werror'。
      如果格式错误, 打印错误并返回 True。
    - check_mode=False (应用模式):
      运行 clang-format, 捕获其输出, 与原文件比较。
      如果文件被修改, 打印 [FORMATTED] 并返回 True。
      
    - 返回 'True' 如果 (check_mode 且格式有误) 或 (apply_mode 且文件被修改)。
    """
    
    # --- 1. 检查模式 (用于 CI) ---
    if check_mode:
        cmd = [CLANG_FORMAT_BIN, "--dry-run", "-Werror", str(file_path)]
        try:
            # 隐藏 stdout, 只在出错时捕获 stderr
            subprocess.run(cmd, check=True, capture_output=True, text=True, encoding="utf-8")
        except subprocess.CalledProcessError as e:
            # -Werror 使 clang-format 在格式不正确时返回非零码
            print(f"  [BAD FORMAT] {file_path}")
            # clang-format 可能会在 stdout 中输出 diff
            if e.stdout:
                print(e.stdout)
            return True # 报告 "格式有误"
        except FileNotFoundError:
            print(f"  [ERROR] 命令 '{CLANG_FORMAT_BIN}' 未找到。", file=sys.stderr)
            sys.exit(2)
        return False # 格式正确

    # --- 2. 应用模式 (用于本地) ---
    try:
        # a. 读取原始文件内容
        original_content = file_path.read_text(encoding="utf-8")
        
        # b. 运行 clang-format (不带 -i), 捕获其 stdout
        #    这将返回它 "认为" 文件应该是什么样子
        cmd = [CLANG_FORMAT_BIN, str(file_path)]
        result = subprocess.run(cmd, check=True, capture_output=True, text=True, encoding="utf-8")
        formatted_content = result.stdout
        
        # c. 比较
        if original_content == formatted_content:
            return False # 无需修改
            
        # d. 内容不同, 执行写入
        print(f"  [FORMATTED] {file_path}")
        file_path.write_text(formatted_content, encoding="utf-8")
        return True # 报告 "已修改"

    except subprocess.CalledProcessError as e:
        # clang-format 无法解析文件
        print(f"  [ERROR] 格式化 {file_path} 失败: {e.stderr}", file=sys.stderr)
        return False
    except FileNotFoundError:
        print(f"  [ERROR] 命令 '{CLANG_FORMAT_BIN}' 未找到。", file=sys.stderr)
        sys.exit(2)
    except IOError as e:
        print(f"  [ERROR] 读/写 {file_path} 失败: {e}", file=sys.stderr)
        return False


def is_excluded(relative_path_str: str) -> bool:
    """
    检查文件路径是否在排除列表中。
    """
    for exclusion_prefix in THIRD_PARTY_PATHS:
        if relative_path_str.startswith(exclusion_prefix):
            return True
    return False

def main():
    """
    [!!] 这个 main 函数现在与你的 clean_comments.py 的结构一致。
    """
    check_mode = "--check" in sys.argv
    project_root = Path(__file__).parent.parent
    
    print("--- Calico-IR 代码格式化 ---")
    if check_mode:
        print("模式: 检查 (Check-Only)")
    else:
        print("模式: 应用 (Apply Format)")

    total_files_processed = 0
    total_files_skipped = 0
    total_files_changed_or_failed = 0 # 跟踪被修改(apply)或格式错误(check)的文件

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
                
                # [!!] 像 clean_comments 一样, 边扫描边处理
                if process_file(file_path, check_mode):
                    total_files_changed_or_failed += 1
        
        if files_in_dir_processed == 0:
            print("  (未找到需要处理的目标文件)")

    print("\n--- 处理完成 ---")
    print(f"总共处理文件: {total_files_processed}")
    print(f"总共跳过文件: {total_files_skipped} (第三方, e.g., xxhash)")
    
    if check_mode:
        if total_files_changed_or_failed > 0:
            print(f"[!!] 失败: {total_files_changed_or_failed} 个文件格式不正确。")
            sys.exit(1) # 退出码 1 (失败), 用于 CI
        else:
            print("[OK] 所有被处理的文件均符合格式规范。")
    else:
        # [!!] 现在有了正确的计数
        print(f"总共格式化文件: {total_files_changed_or_failed}")
        print("[OK] 格式化完成。")
            
    sys.exit(0) # 退出码 0 (成功)

if __name__ == "__main__":
    main()
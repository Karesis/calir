#!/usr/bin/env python3
import sys
from pathlib import Path

# -----------------------------------------------------------------
# [!!] 配置: 填入你的信息
# -----------------------------------------------------------------
COPYRIGHT_YEAR = "2025"
COPYRIGHT_OWNER = "Karesis"

# 要扫描的文件夹
DIRS_TO_SCAN = ["src", "include", "tests"]

# 目标文件扩展名
TARGET_EXTS = {".c", ".h"}

# -----------------------------------------------------------------
# [!!] 模板: 这是将要被添加的 Apache 2.0 声明头
# -----------------------------------------------------------------
BOILERPLATE = f"""/*
 * Copyright {COPYRIGHT_YEAR} {COPYRIGHT_OWNER}
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
"""

# 我们会在头部和代码之间添加两个换行符
BOILERPLATE_WITH_SPACING = BOILERPLATE + "\n\n"

def process_file(file_path: Path, check_mode: bool) -> bool:
    """
    处理单个文件。
    - 检查头部是否存在。
    - 如果 'check_mode' 为 True, 只报告是否缺失。
    - 如果 'check_mode' 为 False, 自动添加缺失的头部。
    - 返回 'True' 如果头部缺失 (无论是否修复)。
    """
    try:
        content = file_path.read_text(encoding="utf-8")
    except (IOError, UnicodeDecodeError) as e:
        print(f"  [ERROR] 无法读取 {file_path}: {e}", file=sys.stderr)
        return False # 跳过

    if content.startswith(BOILERPLATE):
        # 头部已存在，一切正常
        return False
    
    # 头部缺失！
    if check_mode:
        # 只报告
        print(f"  [MISSING] {file_path}")
        return True
    
    # 应用模式：添加头部
    print(f"  [APPLYING] {file_path}")
    try:
        new_content = BOILERPLATE_WITH_SPACING + content
        file_path.write_text(new_content, encoding="utf-8")
        return True # 报告已修复
    except IOError as e:
        print(f"  [ERROR] 无法写入 {file_path}: {e}", file=sys.stderr)
        return True # 仍然算作"缺失"

def main():
    check_mode = "--check" in sys.argv
    project_root = Path(__file__).parent.parent # 脚本在 'scripts/' 目录中

    print("--- Calico-IR 许可证头部检查 ---")
    if check_mode:
        print("模式: 检查 (Check-Only)")
    else:
        print("模式: 应用 (Apply)")

    missing_files = 0
    total_files = 0

    for dir_name in DIRS_TO_SCAN:
        search_dir = project_root / dir_name
        if not search_dir.is_dir():
            print(f"\n[WARN] 目录 '{dir_name}' 不存在, 跳过。")
            continue
            
        print(f"\nScanning {search_dir}...")
        
        # 递归搜索所有 .c 和 .h 文件
        files_in_dir = 0
        for ext in TARGET_EXTS:
            for file_path in search_dir.rglob(f"*{ext}"):
                total_files += 1
                files_in_dir += 1
                if process_file(file_path, check_mode):
                    missing_files += 1
        
        if files_in_dir == 0:
            print("  (未找到目标文件)")

    print("\n--- 扫描完成 ---")
    print(f"总共扫描文件: {total_files}")
    if check_mode:
        if missing_files > 0:
            print(f"[!!] 失败: {missing_files} 个文件缺失许可证头部。")
            sys.exit(1) # 退出码 1 (失败)
        else:
            print("[OK] 所有文件均包含许可证头部。")
    else:
        if missing_files > 0:
            print(f"[OK] 已修复 {missing_files} 个文件。")
        else:
            print("[OK] 所有文件已包含许可证头部。")
            
    sys.exit(0) # 退出码 0 (成功)

if __name__ == "__main__":
    main()
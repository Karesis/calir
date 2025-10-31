// tests/test_parser.c
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "ir/context.h"
#include "ir/function.h"
#include "ir/module.h"
#include "ir/parser.h" // 包含 parser
#include "ir/type.h"

#include "ir_test_helpers.h" // [!!] 新增: 包含我们的黄金 IR 助手
#include "test_utils.h"      // [!!] 新增: 包含我们的测试框架
#include "utils/bump.h"      // [!!] 新增: 需要 arena

/**
 * @brief 自动化测试：
 * 验证 "Round-Trip" (字符串 -> 解析 -> 打印 -> 字符串) 是否 100% 保持不变。
 */
int
test_parser_roundtrip()
{
  SUITE_START("IR Parser: Golden Round-Trip");

  // 1. 设置
  Bump arena;
  bump_init(&arena); // [!!] 为 ...dump_to_string 创建一个 arena
  IRContext *ctx = ir_context_create();

  // 2. 获取黄金标准字符串 (使用 helper)
  const char *golden_text = get_golden_ir_text();

  printf("  (Parsing %zu bytes of golden IR...)\n", strlen(golden_text));

  // 3. [!!] 关键测试：调用 Parser [!!]
  IRModule *parsed_module = ir_parse_module(ctx, golden_text);

  // 4. [!!] 自动化检查：Parser 是否失败？ [!!]
  SUITE_ASSERT(parsed_module != NULL, "ir_parse_module() returned NULL. Parser failed.");

  if (parsed_module)
  {
    // 5. [!!] 关键测试：重新打印为字符串 [!!]
    const char *dumped_str = ir_module_dump_to_string(parsed_module, &arena);

    // 6. [!!] 自动化比较 (不再需要 "肉眼") [!!]
    SUITE_ASSERT(dumped_str != NULL, "ir_module_dump_to_string() returned NULL");
    if (dumped_str)
    {
      SUITE_ASSERT(strcmp(dumped_str, golden_text) == 0,
                   "Parser round-trip output does not match golden string.\n"
                   "\n--- [!!] EXPECTED GOLDEN [!!] ---\n%s\n"
                   "--- [!!] PARSED & RE-PRINTED [!!] ---\n%s\n",
                   golden_text, dumped_str);
    }
  }

  // 7. 清理
  ir_context_destroy(ctx);
  bump_destroy(&arena);

  SUITE_END();
}

int
main()
{
  __calir_current_suite_name = "IR Parser";
  __calir_total_suites_run++;
  if (test_parser_roundtrip() != 0)
  {
    __calir_total_suites_failed++;
  }
  TEST_SUMMARY();
}
// tests/test_ir_printer.c
#include "ir/builder.h"
#include "ir/context.h"
#include "ir/module.h"       // [!!] 需要新的 ...dump_to_string API
#include "ir_test_helpers.h" // [!!] 包含我们的黄金 IR 助手
#include "test_utils.h"      // [!!] 包含我们的测试框架
#include "utils/bump.h"      // [!!] 需要 arena

/**
 * @brief 自动化测试：
 * 验证 build_golden_ir() 的输出是否与 get_golden_ir_text() 完全匹配。
 * 这可以确保我们的 Builder 和 Printer 是同步的。
 */
int
test_print_golden_ir()
{
  SUITE_START("IR Printer: Golden Output");

  // 1. 设置
  Bump arena;
  bump_init(&arena); // [!!] 为 ...dump_to_string 创建一个 arena
  IRContext *ctx = ir_context_create();
  IRBuilder *builder = ir_builder_create(ctx);

  // 2. 构建内存中的 IR (使用 helper)
  IRModule *mod = build_golden_ir(ctx, builder);

  // 3. 打印到字符串 (使用我们重构的新 API)
  const char *dumped_str = ir_module_dump_to_string(mod, &arena);

  // 4. 获取黄金标准字符串 (使用 helper)
  const char *expected_str = get_golden_ir_text();

  // 5. [!!] 自动化比较 (不再需要 "肉眼") [!!]
  SUITE_ASSERT(dumped_str != NULL, "ir_module_dump_to_string() returned NULL");
  if (dumped_str)
  {
    SUITE_ASSERT(strcmp(dumped_str, expected_str) == 0,
                 "Printer output does not match golden string.\n"
                 "\n--- [!!] EXPECTED GOLDEN [!!] ---\n%s\n"
                 "--- [!!] ACTUAL OUTPUT [!!] ---\n%s\n",
                 expected_str, dumped_str);
  }

  // 6. 清理
  ir_builder_destroy(builder);
  ir_context_destroy(ctx);
  bump_destroy(&arena);

  SUITE_END();
}

int
main()
{
  __calir_current_suite_name = "IR Printer";
  __calir_total_suites_run++;
  if (test_print_golden_ir() != 0)
  {
    __calir_total_suites_failed++;
  }
  TEST_SUMMARY();
}
/*
 * Copyright 2025 Karesis
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

#include "ir/builder.h"
#include "ir/context.h"
#include "ir/module.h"
#include "ir_test_helpers.h"
#include "test_utils.h"
#include "utils/bump.h"

/**
 * @brief 自动化测试：
 * 验证 build_golden_ir() 的输出是否与 get_golden_ir_text() 完全匹配。
 * 这可以确保我们的 Builder 和 Printer 是同步的。
 */
int
test_print_golden_ir()
{
  SUITE_START("IR Printer: Golden Output");

  Bump arena;
  bump_init(&arena);
  IRContext *ctx = ir_context_create();
  IRBuilder *builder = ir_builder_create(ctx);

  IRModule *mod = build_golden_ir(ctx, builder);

  const char *dumped_str = ir_module_dump_to_string(mod, &arena);

  const char *expected_str = get_golden_ir_text();

  SUITE_ASSERT(dumped_str != NULL, "ir_module_dump_to_string() returned NULL");
  if (dumped_str)
  {
    SUITE_ASSERT(strcmp(dumped_str, expected_str) == 0,
                 "Printer output does not match golden string.\n"
                 "\n--- [!!] EXPECTED GOLDEN [!!] ---\n%s\n"
                 "--- [!!] ACTUAL OUTPUT [!!] ---\n%s\n",
                 expected_str, dumped_str);
  }

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
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



#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "ir/context.h"
#include "ir/function.h"
#include "ir/module.h"
#include "ir/parser.h"
#include "ir/type.h"

#include "ir_test_helpers.h"
#include "test_utils.h"
#include "utils/bump.h"

/**
 * @brief 自动化测试：
 * 验证 "Round-Trip" (字符串 -> 解析 -> 打印 -> 字符串) 是否 100% 保持不变。
 */
int
test_parser_roundtrip()
{
  SUITE_START("IR Parser: Golden Round-Trip");


  Bump arena;
  bump_init(&arena);
  IRContext *ctx = ir_context_create();


  const char *golden_text = get_golden_ir_text();

  printf("  (Parsing %zu bytes of golden IR...)\n", strlen(golden_text));


  IRModule *parsed_module = ir_parse_module(ctx, golden_text);


  SUITE_ASSERT(parsed_module != NULL, "ir_parse_module() returned NULL. Parser failed.");

  if (parsed_module)
  {

    const char *dumped_str = ir_module_dump_to_string(parsed_module, &arena);


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
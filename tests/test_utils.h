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


#ifndef CALIR_TEST_UTILS_H
#define CALIR_TEST_UTILS_H

#include <stdio.h>
#include <string.h>

/*
 * ========================================
 * --- 1. 内部状态 (Internal State) ---
 * ========================================
 *
 * 这些是 .h 文件中的 'static' 变量。
 * 它们在包含此文件的 *每个* .c 文件中都是唯一的实例。
 * 这正是我们想要的：每个 test_*.c 都管理自己的套件计数。
 */

// 全局状态: 跟踪套件的总数
static int __calir_total_suites_run = 0;
static int __calir_total_suites_failed = 0;

// 套件局部状态: 跟踪当前套件的测试
static const char *__calir_current_suite_name = "";
static int __calir_current_suite_tests = 0;
static int __calir_current_suite_passed = 0;

/*
 * ========================================
 * --- 2. 核心宏 (Core Macros) ---
 * ========================================
 */

/**
 * @brief 标记一个测试套件的开始。
 *
 * @param name 套件的名称 (字符串)
 */
#define SUITE_START(name)                                                                                              \
  do                                                                                                                   \
  {                                                                                                                    \
    __calir_current_suite_name = (name);                                                                               \
    __calir_current_suite_tests = 0;                                                                                   \
    __calir_current_suite_passed = 0;                                                                                  \
    printf("\n--- Test Suite: %s ---\n", __calir_current_suite_name);                                                  \
  } while (0)

/**
 * @brief 断言一个条件为真。
 *
 * @param cond 要测试的条件
 * @param msg 失败时打印的格式化消息 (printf 风格)
 * @param ... 消息的参数
 */
#define SUITE_ASSERT(cond, msg, ...)                                                                                   \
  do                                                                                                                   \
  {                                                                                                                    \
    __calir_current_suite_tests++;                                                                                     \
    if (cond)                                                                                                          \
    {                                                                                                                  \
      __calir_current_suite_passed++;                                                                                  \
      /* 成功时保持安静 */                                                                                             \
    }                                                                                                                  \
    else                                                                                                               \
    {                                                                                                                  \
      /* [!!] 失败时打印详细信息 [!!] */                                                                               \
      fprintf(stderr, "    [FAIL] %s() at line %d:\n", __func__, __LINE__);                                            \
      fprintf(stderr, "           Condition: %s\n", #cond);                                                            \
      fprintf(stderr, "           Message:   ");                                                                       \
      fprintf(stderr, msg, ##__VA_ARGS__);                                                                             \
      fprintf(stderr, "\n");                                                                                           \
    }                                                                                                                  \
  } while (0)

/**
 * @brief 标记一个测试套件的结束。
 *
 * 打印此套件的摘要，并返回一个状态码 (0=成功, 1=失败)。
 */
#define SUITE_END()                                                                                                    \
  do                                                                                                                   \
  {                                                                                                                    \
    printf("--- Summary (%s): %d / %d passed ---\n", __calir_current_suite_name, __calir_current_suite_passed,         \
           __calir_current_suite_tests);                                                                               \
    if (__calir_current_suite_tests == __calir_current_suite_passed)                                                   \
    {                                                                                                                  \
      return 0; /* 成功 */                                                                                             \
    }                                                                                                                  \
    else                                                                                                               \
    {                                                                                                                  \
      return 1; /* 失败 */                                                                                             \
    }                                                                                                                  \
  } while (0)

/*
 * ========================================
 * --- 3. 运行器宏 (Runner Macros for main()) ---
 * ========================================
 */

/**
 * @brief 在 main() 的末尾打印所有测试的最终摘要。
 *
 * @return 0 (如果所有套件都通过), 1 (如果有任何套件失败)
 */
#define TEST_SUMMARY()                                                                                                 \
  do                                                                                                                   \
  {                                                                                                                    \
    printf("\n========================================\n");                                                            \
    printf("  Total Suites Run:    %d\n", __calir_total_suites_run);                                                   \
    printf("  Total Suites Failed: %d\n", __calir_total_suites_failed);                                                \
    printf("========================================\n\n");                                                            \
    if (__calir_total_suites_failed == 0)                                                                              \
    {                                                                                                                  \
      printf("[OK] All %s tests passed.\n", __calir_current_suite_name); /* (current_suite_name 在这里是 "Bitset") */  \
      return 0;                                                                                                        \
    }                                                                                                                  \
    else                                                                                                               \
    {                                                                                                                  \
      fprintf(stderr, "[!!!] %d suite(s) FAILED.\n", __calir_total_suites_failed);                                     \
      return 1;                                                                                                        \
    }                                                                                                                  \
  } while (0)

#endif // CALIR_TEST_UTILS_H
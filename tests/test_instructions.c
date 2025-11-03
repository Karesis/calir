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

/*
 * =================================================================
 * --- 指令覆盖率 ---
 * =================================================================
 *
 * 这个文件测试所有的 IR 指令是否能被 Lexer, Parser,
 * Builder 和 Verifier 正确地端到端处理。
 */

#include "ir/context.h"
#include "ir/module.h"
#include "ir/parser.h"
#include "utils/bump.h"
#include <stdio.h>
#include <string.h>

#include "test_utils.h"

/**
 * @brief [辅助] 运行一个单独的 IR 字符串解析测试
 *
 * 这会创建一个专用的上下文，解析，然后（如果成功）销毁模块。
 *
 * @param arena 用于此测试的 Arena (来自 main)
 * @param name 测试的断言名称 (e.g., "mul")
 * @param ir_string 要解析的 IR 文本
 */
static void
run_test(const char *name, const char *ir_string)
{
  IRContext *ctx = ir_context_create();
  SUITE_ASSERT(ctx != NULL, "Failed to create IRContext for test '%s'", name);

  IRModule *mod = ir_parse_module(ctx, ir_string);

  SUITE_ASSERT(mod != NULL, "Failed to parse/verify snippet for: %s", name);

  ir_context_destroy(ctx);
}

/**
 * @brief 测试所有整数和位二元运算
 */
int
test_integer_binary_ops()
{
  SUITE_START("Parser: Integer/Bitwise Ops");

  /// --- 整数 ---
  run_test("mul", "define i32 @test(%a: i32, %b: i32) {\n"
                  "$entry:\n"
                  "  %r: i32 = mul %a: i32, %b: i32\n"
                  "  ret %r: i32\n"
                  "}\n");

  run_test("sdiv", "define i32 @test(%a: i32, %b: i32) {\n"
                   "$entry:\n"
                   "  %r: i32 = sdiv %a: i32, %b: i32\n"
                   "  ret %r: i32\n"
                   "}\n");

  run_test("udiv", "define i32 @test(%a: i32, %b: i32) {\n"
                   "$entry:\n"
                   "  %r: i32 = udiv %a: i32, %b: i32\n"
                   "  ret %r: i32\n"
                   "}\n");

  run_test("srem", "define i32 @test(%a: i32, %b: i32) {\n"
                   "$entry:\n"
                   "  %r: i32 = srem %a: i32, %b: i32\n"
                   "  ret %r: i32\n"
                   "}\n");

  run_test("urem", "define i32 @test(%a: i32, %b: i32) {\n"
                   "$entry:\n"
                   "  %r: i32 = urem %a: i32, %b: i32\n"
                   "  ret %r: i32\n"
                   "}\n");

  /// --- 位运算 ---
  run_test("shl", "define i32 @test(%a: i32, %b: i32) {\n"
                  "$entry:\n"
                  "  %r: i32 = shl %a: i32, %b: i32\n"
                  "  ret %r: i32\n"
                  "}\n");

  run_test("lshr", "define i32 @test(%a: i32, %b: i32) {\n"
                   "$entry:\n"
                   "  %r: i32 = lshr %a: i32, %b: i32\n"
                   "  ret %r: i32\n"
                   "}\n");

  run_test("ashr", "define i32 @test(%a: i32, %b: i32) {\n"
                   "$entry:\n"
                   "  %r: i32 = ashr %a: i32, %b: i32\n"
                   "  ret %r: i32\n"
                   "}\n");

  run_test("and", "define i32 @test(%a: i32, %b: i32) {\n"
                  "$entry:\n"
                  "  %r: i32 = and %a: i32, %b: i32\n"
                  "  ret %r: i32\n"
                  "}\n");

  run_test("or", "define i32 @test(%a: i32, %b: i32) {\n"
                 "$entry:\n"
                 "  %r: i32 = or %a: i32, %b: i32\n"
                 "  ret %r: i32\n"
                 "}\n");

  run_test("xor", "define i32 @test(%a: i32, %b: i32) {\n"
                  "$entry:\n"
                  "  %r: i32 = xor %a: i32, %b: i32\n"
                  "  ret %r: i32\n"
                  "}\n");

  SUITE_END();
}

/**
 * @brief 测试所有浮点二元运算
 */
int
test_float_binary_ops()
{
  SUITE_START("Parser: Float Ops");

  run_test("fadd", "define f32 @test(%a: f32, %b: f32) {\n"
                   "$entry:\n"
                   "  %r: f32 = fadd %a: f32, %b: f32\n"
                   "  ret %r: f32\n"
                   "}\n");

  run_test("fsub", "define f64 @test(%a: f64, %b: f64) {\n"
                   "$entry:\n"
                   "  %r: f64 = fsub %a: f64, %b: f64\n"
                   "  ret %r: f64\n"
                   "}\n");

  run_test("fmul", "define f32 @test(%a: f32, %b: f32) {\n"
                   "$entry:\n"
                   "  %r: f32 = fmul %a: f32, %b: f32\n"
                   "  ret %r: f32\n"
                   "}\n");

  run_test("fdiv", "define f64 @test(%a: f64, %b: f64) {\n"
                   "$entry:\n"
                   "  %r: f64 = fdiv %a: f64, %b: f64\n"
                   "  ret %r: f64\n"
                   "}\n");

  SUITE_END();
}

/**
 * @brief 测试所有类型转换指令
 */
int
test_cast_ops()
{
  SUITE_START("Parser: Cast Ops");

  run_test("trunc (i32 to i8)", "define i8 @test(%a: i32) {\n"
                                "$entry:\n"
                                "  %r: i8 = trunc %a: i32 to i8\n"
                                "  ret %r: i8\n"
                                "}\n");

  run_test("zext (i8 to i64)", "define i64 @test(%a: i8) {\n"
                               "$entry:\n"
                               "  %r: i64 = zext %a: i8 to i64\n"
                               "  ret %r: i64\n"
                               "}\n");

  run_test("sext (i1 to i32)", "define i32 @test(%a: i1) {\n"
                               "$entry:\n"
                               "  %r: i32 = sext %a: i1 to i32\n"
                               "  ret %r: i32\n"
                               "}\n");

  run_test("fptrunc (f64 to f32)", "define f32 @test(%a: f64) {\n"
                                   "$entry:\n"
                                   "  %r: f32 = fptrunc %a: f64 to f32\n"
                                   "  ret %r: f32\n"
                                   "}\n");

  run_test("fpext (f32 to f64)", "define f64 @test(%a: f32) {\n"
                                 "$entry:\n"
                                 "  %r: f64 = fpext %a: f32 to f64\n"
                                 "  ret %r: f64\n"
                                 "}\n");

  run_test("fptoui (f32 to i32)", "define i32 @test(%a: f32) {\n"
                                  "$entry:\n"
                                  "  %r: i32 = fptoui %a: f32 to i32\n"
                                  "  ret %r: i32\n"
                                  "}\n");

  run_test("fptosi (f64 to i8)", "define i8 @test(%a: f64) {\n"
                                 "$entry:\n"
                                 "  %r: i8 = fptosi %a: f64 to i8\n"
                                 "  ret %r: i8\n"
                                 "}\n");

  run_test("uitofp (i32 to f64)", "define f64 @test(%a: i32) {\n"
                                  "$entry:\n"
                                  "  %r: f64 = uitofp %a: i32 to f64\n"
                                  "  ret %r: f64\n"
                                  "}\n");

  run_test("sitofp (i8 to f32)", "define f32 @test(%a: i8) {\n"
                                 "$entry:\n"
                                 "  %r: f32 = sitofp %a: i8 to f32\n"
                                 "  ret %r: f32\n"
                                 "}\n");

  run_test("ptrtoint (<i32> to i64)", "define i64 @test(%a: <i32>) {\n"
                                      "$entry:\n"
                                      "  %r: i64 = ptrtoint %a: <i32> to i64\n"
                                      "  ret %r: i64\n"
                                      "}\n");

  run_test("inttoptr (i64 to <f32>)", "define <f32> @test(%a: i64) {\n"
                                      "$entry:\n"
                                      "  %r: <f32> = inttoptr %a: i64 to <f32>\n"
                                      "  ret %r: <f32>\n"
                                      "}\n");

  run_test("bitcast (i32 to f32)", "define f32 @test(%a: i32) {\n"
                                   "$entry:\n"
                                   "  %r: f32 = bitcast %a: i32 to f32\n"
                                   "  ret %r: f32\n"
                                   "}\n");

  SUITE_END();
}

/**
 * @brief 测试新的比较和终结者
 */
int
test_compare_and_terminator_ops()
{
  SUITE_START("Parser: FCmp / Switch");

  /// --- FCmp ---
  run_test("fcmp oeq", "define i1 @test(%a: f32, %b: f32) {\n"
                       "$entry:\n"
                       "  %r: i1 = fcmp oeq %a: f32, %b: f32\n"
                       "  ret %r: i1\n"
                       "}\n");

  run_test("fcmp ugt", "define i1 @test(%a: f64, %b: f64) {\n"
                       "$entry:\n"
                       "  %r: i1 = fcmp ugt %a: f64, %b: f64\n"
                       "  ret %r: i1\n"
                       "}\n");

  run_test("fcmp uno", "define i1 @test(%a: f32, %b: f32) {\n"
                       "$entry:\n"
                       "  %r: i1 = fcmp uno %a: f32, %b: f32\n"
                       "  ret %r: i1\n"
                       "}\n");

  /// --- Switch ---
  run_test("switch", "define void @test(%a: i32) {\n"
                     "$entry:\n"
                     "  switch %a: i32, default $l_end [\n"
                     "    10: i32, $l_case1\n"
                     "    20: i32, $l_case2\n"
                     "    30: i32, $l_end\n"
                     "  ]\n"
                     "$l_case1:\n"
                     "  br $l_end\n"
                     "$l_case2:\n"
                     "  br $l_end\n"
                     "$l_end:\n"
                     "  ret void\n"
                     "}\n");

  SUITE_END();
}

/**
 * @brief 主测试运行器
 */
int
main()
{
  __calir_current_suite_name = "Parser: New Instructions";

  __calir_total_suites_run++;
  if (test_integer_binary_ops() != 0)
  {
    __calir_total_suites_failed++;
  }

  __calir_total_suites_run++;
  if (test_float_binary_ops() != 0)
  {
    __calir_total_suites_failed++;
  }

  __calir_total_suites_run++;
  if (test_cast_ops() != 0)
  {
    __calir_total_suites_failed++;
  }

  __calir_total_suites_run++;
  if (test_compare_and_terminator_ops() != 0)
  {
    __calir_total_suites_failed++;
  }
  TEST_SUMMARY();
}

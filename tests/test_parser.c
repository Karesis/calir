/* tests/test_parser.c */
#include <assert.h>
#include <stdio.h>

#include "ir/context.h"
#include "ir/module.h"
#include "ir/parser.h"

/**
 * @brief 测试模块名回退 (不提供 source_filename)
 */
static void
test_module_id_fallback(void)
{
  printf("--- Running Test: test_module_id_fallback ---\n");
  const char *source = "define void @main() {\n"
                       "entry: \n" // [!!] 必须显式添加
                       "  ret void\n"
                       "}\n";

  IRContext *ctx = ir_context_create();
  assert(ctx != NULL);

  IRModule *module = ir_parse_module(ctx, source);
  assert(module != NULL && "Parser failed on valid minimal IR");

  // 验证模块名是否为 "parsed_module" (你在 ir_parse_module 中的默认值)
  assert(strcmp(module->name, "parsed_module") == 0 && "Module name fallback failed");

  printf("Module (fallback name):\n");
  ir_module_dump(module, stdout);

  ir_context_destroy(ctx);
  printf("--- PASS ---\n\n");
}

/**
 * @brief "黄金"测试: 解析一个包含所有功能的复杂模块
 */
static void
test_parse_success_golden(void)
{
  printf("--- Running Test: test_parse_success_golden ---\n");

  // 这个字符串测试我们实现的所有功能
  const char *golden_source = "; --- 模块ID 和类型 ---\n"
                              "source_filename = \"golden.c\"\n"
                              "type %MyStruct = { i32, ptr i8, [4 x f32] }\n"
                              "\n"
                              "; --- 全局变量和声明 ---\n"
                              "@g_my_global = global i32 123\n"
                              "declare void @my_print(i32, f64, ptr %MyStruct)\n"
                              "\n"
                              "; --- 函数定义 ---\n"
                              "define i32 @main(i32 %argc, i64 %argv) {\n"
                              "\n"
                              "entry:\n"
                              "  ; [!! 已修复 !!] 所有 alloca 都必须在 entry 块\n"
                              "  %p.struct = alloca %MyStruct\n"
                              "  %p.int = alloca i32\n"
                              "  %arr.ptr = alloca [10 x i32]\n"
                              "\n"
                              "  ; 测试 store, load, i1, f64, null, undef\n"
                              "  store i32 10, ptr i32 %p.int\n"
                              "  %a = load i32, ptr i32 %p.int\n"
                              "  %b = add i32 %a, 20\n"
                              "  %cmp = icmp slt i32 %b, 100\n"
                              "  br i1 %cmp, label %if_true, label %if_false\n"
                              "\n"
                              "if_true:\n"
                              "  ; 测试 GEP (struct), call, float\n"
                              "  %f.ptr = gep %MyStruct, ptr %MyStruct %p.struct, i32 0, i32 2\n"
                              "  call @my_print(i32 1, f64 3.14, ptr %MyStruct undef)\n"
                              "  br label %if_end\n"
                              "\n"
                              "if_false:\n"
                              "  ; 测试 GEP (array), sub, icmp (unsigned)\n"
                              "  %idx = sub i32 5, 1\n"
                              "  %elem.ptr = gep [10 x i32], ptr [10 x i32] %arr.ptr, i64 0, i32 %idx\n"
                              "  %cmp.u = icmp uge i32 %idx, 0\n"
                              "  call @my_print(i32 0, f64 -0.5, ptr %MyStruct undef)\n"
                              "  br label %if_end\n"
                              "\n"
                              "if_end:\n"
                              "  ; 测试 PHI 和 ret\n"
                              "  %phi.res = phi i32 [ 100, %if_true ], [ 200, %if_false ]\n"
                              "  ret i32 %phi.res\n"
                              "}\n";

  IRContext *ctx = ir_context_create();
  assert(ctx != NULL);

  IRModule *module = ir_parse_module(ctx, golden_source);

  // 如果解析失败 (e.g., verifier 失败), module 会是 NULL
  assert(module != NULL && "GOLDEN TEST FAILED: Parser returned NULL");

  // 验证模块名
  assert(strcmp(module->name, "golden.c") == 0 && "Module name parsing failed");

  // 核心验证: 将解析出的 IR 重新 dump 出来
  // 我们可以“目视”检查 dump 的输出是否与输入一致
  printf("Golden Module Dump:\n");
  printf("=====================\n");
  ir_module_dump(module, stdout);
  printf("=====================\n");

  ir_context_destroy(ctx);
  printf("--- PASS ---\n\n");
}

/**
 * @brief 测试预期的解析失败 (e.g., 语法/语义错误)
 */
static void
run_fail_test(const char *name, const char *source)
{
  printf("--- Running Test: %s (EXPECTED TO FAIL) ---\n", name);
  IRContext *ctx = ir_context_create();
  assert(ctx != NULL);

  IRModule *module = ir_parse_module(ctx, source);

  // [!!] 核心断言: 我们 *期望* 解析失败
  assert(module == NULL && "Parser succeeded on an invalid IR string");

  ir_context_destroy(ctx);
  printf("--- PASS (Parser correctly failed) ---\n\n");
}

/**
 * @brief 测试所有预期失败的用例
 */
static void
test_parser_failures(void)
{
  // 1. 语法错误: 缺少 '}'
  run_fail_test("test_fail_syntax", "define void @main() { ret void");

  // 2. 语义错误: 类型不匹配 (ret i32, 但函数返回 void)
  run_fail_test("test_fail_type_mismatch", "define void @main() {\n"
                                           "%entry:\n"
                                           "  ret i32 0\n"
                                           "}\n");

  // 3. 语义错误: 使用未定义的变量
  run_fail_test("test_fail_undefined_value", "define i32 @main() {\n"
                                             "%entry:\n"
                                             "  %a = add i32 1, %unbound\n"
                                             "  ret i32 %a\n"
                                             "}\n");

  // 4. 语义错误: 标签重定义
  run_fail_test("test_fail_redefinition", "define void @main() {\n"
                                          "%entry:\n"
                                          "  br label %entry\n"
                                          "%entry:\n"
                                          "  ret void\n"
                                          "}\n");

  // 5. 语义错误: GEP 类型不匹配 (ptr i32 != ptr [10 x i32])
  run_fail_test("test_fail_gep_mismatch", "define void @main() {\n"
                                          "%entry:\n"
                                          "  %p = alloca i32\n"
                                          "  %elem = gep [10 x i32], ptr %p, i64 0, i32 1\n"
                                          "  ret void\n"
                                          "}\n");

  // 6. 语义错误: Call 参数数量不匹配
  run_fail_test("test_fail_call_arg_count", "declare void @foo(i32)\n"
                                            "define void @main() {\n"
                                            "%entry:\n"
                                            "  call @foo(i32 1, i32 2)\n"
                                            "  ret void\n"
                                            "}\n");

  // 7. 语义错误: 指令在终结者之后
  run_fail_test("test_fail_instr_after_terminator", "define void @main() {\n"
                                                    "%entry:\n"
                                                    "  ret void\n"
                                                    "  %a = alloca i32\n"
                                                    "}\n");
}

/**
 * @brief 主测试函数
 */
int
main(void)
{
  printf("\n=================================================\n");
  printf("========= 🧪 开始测试 Parser 🧪 ==========\n");
  printf("=================================================\n\n");

  test_module_id_fallback();
  test_parse_success_golden();
  test_parser_failures();

  printf("\n=================================================\n");
  printf("========= ✅ Parser 测试全部通过 ✅ ======\n");
  printf("=================================================\n\n");
  return 0;
}
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

// 包含你的 calir 项目的核心头文件
#include "ir/context.h"
#include "ir/function.h"
#include "ir/module.h"
#include "ir/parser.h" // [!!] 包含我们重构的 parser
#include "ir/type.h"

// [!!] 这是你刚刚生成的，100% 统一为 "设计 B" 的黄金 IR [!!]
const char *GOLDEN_IR_TEXT = "module = \"golden_module\"\n"
                             "\n"
                             "%my_struct = type { i32, i32 }\n"
                             "\n"
                             "declare i32 @external_add(%x: i32, %y: i32)\n"
                             "\n"
                             "define i32 @kitchen_sink(%a: i32, %b: i32) {\n"
                             "$entry:\n"
                             "  %struct_ptr: <%my_struct> = alloc %my_struct\n"
                             "  %elem_ptr: <i32> = gep %struct_ptr: <%my_struct>, 0: i32, 1: i32\n"
                             "  store %a: i32, %elem_ptr: <i32>\n"
                             "  %loaded_val: i32 = load %elem_ptr: <i32>\n"
                             "  %cmp: i1 = icmp sgt %loaded_val: i32, %b: i32\n"
                             "  br %cmp: i1, $then, $else\n"
                             "$then:\n"
                             "  %call_res: i32 = call <i32 (i32, i32)> @external_add(%a: i32, 10: i32)\n"
                             "  br $merge\n"
                             "$else:\n"
                             "  %sub_res: i32 = sub %b: i32, 20: i32\n"
                             "  br $merge\n"
                             "$merge:\n"
                             "  %phi_val: i32 = phi [ %call_res: i32, $then ], [ %sub_res: i32, $else ]\n"
                             "  ret %phi_val: i32\n"
                             "}\n";

// (你需要一个简单的 'dump_to_string' 辅助函数，
//  但为了简单起见，我们暂时只依赖肉眼比较)

int
main(int argc, char **argv)
{
  printf("--- Calir IR Round-Trip Test (FINAL V2) ---\n");

  // 1. --- 打印我们的“黄金标准”输入 ---
  printf("--- 1. Golden Input (from Printer V2) ---\n");
  printf("%s", GOLDEN_IR_TEXT);
  printf("-----------------------------------------\n\n");

  // 2. --- 设置 Context ---
  IRContext *ctx = ir_context_create();

  // 3. --- [!!] 关键测试：调用重构后的 Parser [!!] ---
  printf("--- 2. Calling REFACTORED ir_parse_module()... ---\n");
  IRModule *parsed_module = ir_parse_module(ctx, GOLDEN_IR_TEXT);

  // 4. --- 检查 Parser 是否失败 ---
  if (parsed_module == NULL)
  {
    printf("\n[!!] TEST FAILED: ir_parse_module() returned NULL.\n");
    printf("[!!] 我们的 parser.c 重构仍然有 bug。\n");
    printf("[!!] (请检查上面的 'Parse Error' 消息)\n");
  }
  else
  {
    // 5. --- 如果 Parser 成功，打印它构建的 IR ---
    printf("[!!] TEST SUCCEEDED: Parser did not crash.\n");
    printf("\n--- 3. Parsed Output (from Parser -> Printer) ---\n");
    ir_module_dump(parsed_module, stdout);
    printf("---------------------------------------------------\n");
    printf("\n[!!] 请肉眼比较 'Golden Input' 和 'Parsed Output'。\n");
    printf("[!!] 如果它们一模一样，我们就 100%% 成功了！\n");
  }

  // 6. --- 清理 ---
  ir_context_destroy(ctx);

  printf("\n--- Test Finished ---\n");
  return 0;
}
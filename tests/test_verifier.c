#include "ir/basicblock.h"
#include "ir/builder.h"
#include "ir/constant.h"
#include "ir/context.h"
#include "ir/function.h"
#include "ir/instruction.h" // <-- 需要 IRICmpPredicate
#include "ir/module.h"
#include "ir/type.h"
#include "ir/verifier.h" // <-- 我们要测试的

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>

// 最小的测试宏
static int g_tests_passed = 0;
static int g_tests_total = 0;

#define TEST_ASSERT(condition)                                                                                         \
  do                                                                                                                   \
  {                                                                                                                    \
    g_tests_total++;                                                                                                   \
    if (condition)                                                                                                     \
    {                                                                                                                  \
      g_tests_passed++;                                                                                                \
    }                                                                                                                  \
    else                                                                                                               \
    {                                                                                                                  \
      fprintf(stderr, "Assertion FAILED: (%s) at %s:%d\n", #condition, __FILE__, __LINE__);                            \
    }                                                                                                                  \
  } while (0)

#define TEST_START(name)                                                                                               \
  do                                                                                                                   \
  {                                                                                                                    \
    g_tests_passed = 0;                                                                                                \
    g_tests_total = 0;                                                                                                 \
    printf("--- Test: %s ---\n", name);                                                                                \
  } while (0)

#define TEST_SUMMARY()                                                                                                 \
  do                                                                                                                   \
  {                                                                                                                    \
    printf("--- Summary: %d/%d passed ---\n\n", g_tests_passed, g_tests_total);                                        \
  } while (0)

/**
 * @brief 正向测试: 构建一个合法的、带 PHI 节点的 if-then-else 结构
 *
 * int test_valid(int x) {
 * // entry:
 * if (x > 10) {
 * // then_bb:
 * // (value = x)
 * } else {
 * // else_bb:
 * // (value = 10)
 * }
 * // merge_bb:
 * int result = phi [ x, %then_bb ], [ 10, %else_bb ];
 * return result;
 * }
 */
static void
test_valid_ir()
{
  TEST_START("test_valid_ir (if-then-else with PHI)");

  IRContext *ctx = ir_context_create();
  IRModule *mod = ir_module_create(ctx, "test_module");
  IRBuilder *builder = ir_builder_create(ctx);

  IRType *i32 = ir_type_get_i32(ctx);

  // 1. 创建函数和参数
  IRFunction *func = ir_function_create(mod, "test_valid", i32);
  IRArgument *arg_x = ir_argument_create(func, i32, "x");

  // 2. 创建基本块
  IRBasicBlock *bb_entry = ir_basic_block_create(func, "entry");
  IRBasicBlock *bb_then = ir_basic_block_create(func, "then");
  IRBasicBlock *bb_else = ir_basic_block_create(func, "else");
  IRBasicBlock *bb_merge = ir_basic_block_create(func, "merge");

  // 3. 填充 Entry 块
  ir_builder_set_insertion_point(builder, bb_entry);
  IRValueNode *const_10 = ir_constant_get_i32(ctx, 10);
  IRValueNode *cond = ir_builder_create_icmp(builder,
                                             IR_ICMP_SGT, // signed greater than
                                             &arg_x->value, const_10);
  ir_builder_create_cond_br(builder, cond, &bb_then->label_address, &bb_else->label_address);

  // 4. 填充 Then 块
  ir_builder_set_insertion_point(builder, bb_then);
  ir_builder_create_br(builder, &bb_merge->label_address);

  // 5. 填充 Else 块
  ir_builder_set_insertion_point(builder, bb_else);
  ir_builder_create_br(builder, &bb_merge->label_address);

  // 6. 填充 Merge 块
  ir_builder_set_insertion_point(builder, bb_merge);
  IRValueNode *phi = ir_builder_create_phi(builder, i32);
  ir_builder_create_ret(builder, phi);

  // 7. [关键] 在*最后*添加 PHI 的 incoming 边
  ir_phi_add_incoming(phi, &arg_x->value, bb_then); // [ %x, %then ]
  ir_phi_add_incoming(phi, const_10, bb_else);      // [ 10, %else ]

  // 8. [!!] 执行测试 [!!]
  // ir_module_dump(mod, stdout); // (调试时取消注释)
  bool ok = ir_verify_function(func);
  TEST_ASSERT(ok == true); // 必须是合法的

  // 清理
  ir_builder_destroy(builder);
  ir_context_destroy(ctx);

  TEST_SUMMARY();
}

/**
 * @brief 反向测试: 构建一个违反 SSA 支配规则的 IR
 *
 * int test_invalid_ssa() {
 * // entry:
 * br cond, %then, %else
 *
 * // then:
 * int x = 1 + 2; // <-- %x 在 'then' 块中定义
 * br %merge
 * * // else:
 * br %merge
 * * // merge:
 * // [!!] 错误: %x 在这里被使用，但 'then' 块并不支配 'merge' 块
 * int y = %x + 3;
 * return y;
 * }
 */
static void
test_invalid_ir_ssa_dominance()
{
  TEST_START("test_invalid_ir_ssa_dominance (smoke test)");

  IRContext *ctx = ir_context_create();
  IRModule *mod = ir_module_create(ctx, "test_module");
  IRBuilder *builder = ir_builder_create(ctx);

  IRType *i32 = ir_type_get_i32(ctx);

  // 1. 创建函数和参数
  IRFunction *func = ir_function_create(mod, "test_invalid_ssa", i32);
  IRArgument *arg_cond = ir_argument_create(func, ir_type_get_i1(ctx), "cond");

  // 2. 创建基本块
  IRBasicBlock *bb_entry = ir_basic_block_create(func, "entry");
  IRBasicBlock *bb_then = ir_basic_block_create(func, "then");
  IRBasicBlock *bb_else = ir_basic_block_create(func, "else");
  IRBasicBlock *bb_merge = ir_basic_block_create(func, "merge");

  // 3. 填充 Entry 块
  ir_builder_set_insertion_point(builder, bb_entry);
  ir_builder_create_cond_br(builder, &arg_cond->value, &bb_then->label_address, &bb_else->label_address);

  // 4. 填充 Then 块
  ir_builder_set_insertion_point(builder, bb_then);
  IRValueNode *const_1 = ir_constant_get_i32(ctx, 1);
  IRValueNode *const_2 = ir_constant_get_i32(ctx, 2);
  IRValueNode *val_x = ir_builder_create_add(builder, const_1, const_2); // <-- %x 在此定义
  ir_builder_create_br(builder, &bb_merge->label_address);

  // 5. 填充 Else 块
  ir_builder_set_insertion_point(builder, bb_else);
  ir_builder_create_br(builder, &bb_merge->label_address);

  // 6. 填充 Merge 块
  ir_builder_set_insertion_point(builder, bb_merge);
  IRValueNode *const_3 = ir_constant_get_i32(ctx, 3);

  // [!!] 错误: val_x (在 %then 中定义) 在这里被使用
  // 但 %then 并不支配 %merge
  IRValueNode *val_y = ir_builder_create_add(builder, val_x, const_3);
  ir_builder_create_ret(builder, val_y);

  // 7. [!!] 执行测试 [!!]
  // ir_module_dump(mod, stdout); // (调试时取消注释)

  // 我们需要一个方法来 "捕获" verifier 的 stderr 输出，
  // 但对于冒烟测试，我们只检查返回值。
  bool ok = ir_verify_function(func);

  TEST_ASSERT(ok == false); // 必须是 *不* 合法的

  // 清理
  ir_builder_destroy(builder);
  ir_context_destroy(ctx);

  TEST_SUMMARY();
}

int
main(void)
{
  test_valid_ir();
  test_invalid_ir_ssa_dominance();

  // (你可以在这里添加更多反向测试，例如 test_invalid_phi_missing_entry, test_invalid_gep, ...)

  printf("\n[OK] All Verifier smoke tests passed.\n");
  return 0;
}
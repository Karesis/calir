#include "ir/basicblock.h"
#include "ir/builder.h"
#include "ir/constant.h"
#include "ir/context.h"
#include "ir/function.h"
#include "ir/instruction.h"
#include "ir/module.h"
#include "ir/type.h"
#include <assert.h>
#include <stdio.h>

/**
 * @brief 构建一个测试 GEP 指令的函数
 * define void @test_func(i32 %idx)
 */
static void
build_test_function(IRModule *mod)
{
  IRContext *ctx = mod->context;

  // 1. 获取基本类型和常量
  IRType *i32_type = ir_type_get_i32(ctx);
  IRType *i64_type = ir_type_get_i64(ctx);
  IRType *void_type = ir_type_get_void(ctx);

  IRValueNode *const_0 = ir_constant_get_i32(ctx, 0);
  IRValueNode *const_1 = ir_constant_get_i32(ctx, 1);
  IRValueNode *const_123 = ir_constant_get_i32(ctx, 123);

  // 2. [!!] 定义我们的聚合类型

  // %point = type { i32, i64 }
  IRType *point_members[2] = {i32_type, i64_type};
  IRType *point_type = ir_type_get_named_struct(ctx, "point", point_members, 2);

  // %array_type = [10 x i32]
  IRType *array_type = ir_type_get_array(ctx, i32_type, 10);

  // %data_packet = type { %point, [10 x i32] }
  IRType *packet_members[2] = {point_type, array_type};
  IRType *data_packet_type = ir_type_get_named_struct(ctx, "data_packet", packet_members, 2);

  // 3. 创建函数签名
  // define void @test_func(i32 %idx)
  IRFunction *func = ir_function_create(mod, "test_func", void_type);
  IRArgument *arg_idx = ir_argument_create(func, i32_type, "idx");

  // 4. 创建入口
  IRBasicBlock *entry_bb = ir_basic_block_create(func, "entry");
  IRBuilder *builder = ir_builder_create(ctx);
  ir_builder_set_insertion_point(builder, entry_bb);

  // 5. [!!] Alloca 我们的指针
  // %point_ptr = alloca %point
  IRValueNode *point_ptr = ir_builder_create_alloca(builder, point_type);
  // %packet_ptr = alloca %data_packet
  IRValueNode *packet_ptr = ir_builder_create_alloca(builder, data_packet_type);
  // %arr_ptr = alloca [10 x i32]
  IRValueNode *arr_ptr = ir_builder_create_alloca(builder, array_type);

  // 6. [!!] --- Test Case 1: 访问 Struct 成员 ---
  // GEP: %y_ptr = getelementptr %point, ptr %point_ptr, i32 0, i32 1
  IRValueNode *gep_indices_1[] = {const_0, const_1};
  IRValueNode *y_ptr = ir_builder_create_gep(builder, point_type, point_ptr, gep_indices_1, 2, false);
  // 验证: load i64, ptr %y_ptr
  ir_builder_create_load(builder, i64_type, y_ptr);

  // 7. [!!] --- Test Case 2: 嵌套 Struct/Array 访问 ---
  // GEP: %elem_ptr = getelementptr inbounds %data_packet, ptr %packet_ptr, i32 0, i32 1, i32 %idx
  IRValueNode *gep_indices_2[] = {const_0, const_1, &arg_idx->value};
  IRValueNode *elem_ptr = ir_builder_create_gep(builder, data_packet_type, packet_ptr, gep_indices_2, 3,
                                                true); // [!] 测试 'inbounds'
  // 验证: store i32 123, ptr %elem_ptr
  ir_builder_create_store(builder, const_123, elem_ptr);

  // 8. [!!] --- Test Case 3: 纯 Array 访问 ---
  // GEP: %arr_elem_ptr = getelementptr [10 x i32], ptr %arr_ptr, i32 0, i32 %idx
  IRValueNode *gep_indices_3[] = {const_0, &arg_idx->value};
  IRValueNode *arr_elem_ptr = ir_builder_create_gep(builder, array_type, arr_ptr, gep_indices_3, 2, false);
  // 验证: load i32, ptr %arr_elem_ptr
  ir_builder_create_load(builder, i32_type, arr_elem_ptr);

  // 9. 终结者
  ir_builder_create_ret(builder, NULL); // ret void

  // 10. 清理
  ir_builder_destroy(builder);
}

int
main()
{
  printf("--- Calir IR Smoke Test ---\n\n");

  // 1. 初始化 Context
  IRContext *ctx = ir_context_create();
  if (!ctx)
  {
    fprintf(stderr, "Failed to create IRContext\n");
    return 1;
  }

  // 2. 初始化 Module
  IRModule *mod = ir_module_create(ctx, "test_module");

  // 3. 构建我们的 IR
  build_test_function(mod);

  // 4. 转储 (Dump) 整个模块的 IR
  ir_module_dump(mod, stdout);

  // 5. 销毁 Context (这将释放所有 Arena 内存)
  ir_context_destroy(ctx);

  printf("\n--- Test Complete ---\n");
  return 0;
}
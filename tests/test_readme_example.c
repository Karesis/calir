#include "ir/builder.h"
#include "ir/context.h"
#include "ir/function.h"
#include "ir/module.h"
#include "ir/type.h"
#include <stdio.h>

// Build the function
static void
build_test_function(IRModule *mod)
{
  IRContext *ctx = mod->context;

  // 1. Get/Create types
  IRType *i32_type = ir_type_get_i32(ctx);
  IRType *i64_type = ir_type_get_i64(ctx);
  IRType *void_type = ir_type_get_void(ctx);

  // %point = type { i32, i64 }
  IRType *point_members[2] = {i32_type, i64_type};
  IRType *point_type = ir_type_get_named_struct(ctx, "point", point_members, 2);

  // %array_type = [10 x i32]
  IRType *array_type = ir_type_get_array(ctx, i32_type, 10);

  // %data_packet = type { %point, [10 x i32] }
  IRType *packet_members[2] = {point_type, array_type};
  IRType *data_packet_type = ir_type_get_named_struct(ctx, "data_packet", packet_members, 2);

  // 2. Create function and entry
  // define void @test_func(i32 %idx)
  IRFunction *func = ir_function_create(mod, "test_func", void_type);
  IRArgument *arg_idx = ir_argument_create(func, i32_type, "idx");

  IRBasicBlock *entry_bb = ir_basic_block_create(func, "entry");
  IRBuilder *builder = ir_builder_create(ctx);
  ir_builder_set_insertion_point(builder, entry_bb);

  // 3. Alloca
  // %point_ptr = alloc %point
  IRValueNode *point_ptr = ir_builder_create_alloca(builder, point_type);
  // %packet_ptr = alloc %data_packet
  IRValueNode *packet_ptr = ir_builder_create_alloca(builder, data_packet_type);

  // 4. Create GEP and Load/Store
  IRValueNode *const_0 = ir_constant_get_i32(ctx, 0);
  IRValueNode *const_1 = ir_constant_get_i32(ctx, 1);
  IRValueNode *const_123 = ir_constant_get_i32(ctx, 123);

  // %1 = gep inbounds %data_packet, ptr %packet_ptr, i32 0, i32 1, i32 %idx
  IRValueNode *gep_indices[] = {const_0, const_1, &arg_idx->value};
  IRValueNode *elem_ptr =
    ir_builder_create_gep(builder, data_packet_type, packet_ptr, gep_indices, 3, true /* inbounds */);
  // store i32 123, ptr %1
  ir_builder_create_store(builder, const_123, elem_ptr);

  // 5. Terminator
  ir_builder_create_ret(builder, NULL); // ret void
  ir_builder_destroy(builder);
}

// Main function
int
main()
{
  IRContext *ctx = ir_context_create();
  IRModule *mod = ir_module_create(ctx, "test_module");

  build_test_function(mod);

  // 4. [!!] Dump the IR for the entire module
  printf("--- Calir IR Dump ---\n");
  ir_module_dump(mod, stdout);
  printf("--- Dump Complete ---\n");

  ir_context_destroy(ctx);
  return 0;
}
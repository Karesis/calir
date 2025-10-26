#include "ir/builder.h"
#include "ir/basicblock.h"
#include "ir/context.h"
#include "ir/instruction.h"
#include "ir/type.h"
#include "ir/use.h"
#include "utils/bump.h"

#include <assert.h>
#include <stdio.h>  // for snprintf
#include <stdlib.h> // for malloc/free
#include <string.h>

// --- 生命周期 ---

IRBuilder *
ir_builder_create(IRContext *ctx)
{
  assert(ctx != NULL);
  // Builder 本身是 malloc/free 的，它只是一个工具
  IRBuilder *builder = (IRBuilder *)malloc(sizeof(IRBuilder));
  if (!builder)
    return NULL;

  builder->context = ctx;
  builder->insertion_point = NULL;
  builder->next_temp_reg_id = 0;
  return builder;
}

void
ir_builder_destroy(IRBuilder *builder)
{
  if (!builder)
    return;
  free(builder);
}

void
ir_builder_set_insertion_point(IRBuilder *builder, IRBasicBlock *bb)
{
  assert(builder != NULL);
  builder->insertion_point = bb;
}

/*
 * =================================================================
 * --- 内部辅助函数 ---
 * =================================================================
 */

/**
 * @brief [内部] 生成下一个临时寄存器名 (e.g., "%1")
 *
 * 名字被分配在 *IR Arena* 中 (因为它们被 IRInstruction 引用)
 *
 * @param builder Builder
 * @return const char* 指向 Arena 中的字符串
 */
static const char *
builder_get_next_reg_name(IRBuilder *builder)
{
  IRContext *ctx = builder->context;
  char buffer[16]; // "%" + 10-digit number + "\0" (足够了)

  // 1. 生成名字
  // (注意: 在一个真正的编译器中, 0 应该是一个有效的名字)
  snprintf(buffer, sizeof(buffer), "%zu", builder->next_temp_reg_id);
  builder->next_temp_reg_id++;

  // 2. 将名字 Intern (或至少分配在 Arena)
  // 我们使用 intern 来确保 "%1" 总是同一个指针，
  // 尽管对于临时变量，使用 bump_alloc_str 也可以。
  return ir_context_intern_str(ctx, buffer);
}

/**
 * @brief [内部] 分配并初始化指令 (但不创建 Operands)
 * @param builder Builder
 * @param opcode 指令码
 * @param type 指令*结果*的类型 (如果是 void, 使用 ctx->type_void)
 * @return 指向新指令的指针
 */
static IRInstruction *
ir_instruction_create_internal(IRBuilder *builder, IROpcode opcode, IRType *type)
{
  assert(builder != NULL);
  assert(builder->insertion_point != NULL && "Builder insertion point is not set");
  IRContext *ctx = builder->context;

  // 1. [新] 从 ir_arena 分配
  IRInstruction *inst = BUMP_ALLOC_ZEROED(&ctx->ir_arena, IRInstruction);
  if (!inst)
    return NULL; // OOM

  // 2. 初始化基类 (IRValueNode)
  inst->result.kind = IR_KIND_INSTRUCTION;
  inst->result.type = type;
  list_init(&inst->result.uses); // [新] 显式初始化

  // 3. 初始化子类 (IRInstruction)
  inst->opcode = opcode;
  inst->parent = builder->insertion_point;
  list_init(&inst->list_node);
  list_init(&inst->operands);

  // 4. [新] 分配名字 (如果它有结果)
  if (type->kind != IR_TYPE_VOID)
  {
    inst->result.name = builder_get_next_reg_name(builder);
  }
  else
  {
    inst->result.name = NULL;
  }

  // 5. 插入到基本块
  list_add_tail(&builder->insertion_point->instructions, &inst->list_node);

  return inst;
}

/*
 * =================================================================
 * --- 公共 API 实现 ---
 * =================================================================
 */

// --- API: 终结者指令 (Terminators) ---

IRValueNode *
ir_builder_create_ret(IRBuilder *builder, IRValueNode *val)
{
  IRType *void_type = builder->context->type_void;
  IRInstruction *inst = ir_instruction_create_internal(builder, IR_OP_RET, void_type);

  if (val)
  {
    // 'ret <val>'
    ir_use_create(builder->context, inst, val);
  }
  // else 'ret void' (0 个操作数)

  return &inst->result;
}

IRValueNode *
ir_builder_create_br(IRBuilder *builder, IRValueNode *target_bb)
{
  assert(target_bb != NULL);
  assert(target_bb->type->kind == IR_TYPE_LABEL && "br target must be a label");

  IRType *void_type = builder->context->type_void;
  IRInstruction *inst = ir_instruction_create_internal(builder, IR_OP_BR, void_type);

  ir_use_create(builder->context, inst, target_bb);

  return &inst->result;
}

IRValueNode *
ir_builder_create_cond_br(IRBuilder *builder, IRValueNode *cond, IRValueNode *true_bb, IRValueNode *false_bb)
{
  assert(builder != NULL);
  assert(cond != NULL && true_bb != NULL && false_bb != NULL);

  // [!!] 关键断言
  assert(cond->type == ir_type_get_i1(builder->context) && "br condition must be i1");
  assert(true_bb->type->kind == IR_TYPE_LABEL && "br target must be a label");
  assert(false_bb->type->kind == IR_TYPE_LABEL && "br target must be a label");

  // 1. 获取 void 类型 (br 指令没有结果值)
  IRType *void_type = builder->context->type_void;

  // 2. 调用内部工厂函数
  IRInstruction *inst = ir_instruction_create_internal(builder,
                                                       IR_OP_COND_BR, // [!!] 新的 Opcode
                                                       void_type);

  if (!inst)
    return NULL; // OOM

  // 3. 创建 Use 边 (链接 3 个操作数)
  ir_use_create(builder->context, inst, cond);     // Operand 0: 条件
  ir_use_create(builder->context, inst, true_bb);  // Operand 1: true 分支
  ir_use_create(builder->context, inst, false_bb); // Operand 2: false 分支

  // 4. 返回指令的 "结果" (一个无名、void 类型的 ValueNode)
  return &inst->result;
}

// --- API: 二元运算 ---

// 辅助函数
static IRValueNode *
builder_create_binary_op(IRBuilder *builder, IROpcode op, IRValueNode *lhs, IRValueNode *rhs)
{
  assert(lhs != NULL && rhs != NULL);
  assert(lhs->type == rhs->type && "Binary operands must have the same type");

  // 结果类型与操作数类型相同
  IRInstruction *inst = ir_instruction_create_internal(builder, op, lhs->type);

  ir_use_create(builder->context, inst, lhs); // Operand 0
  ir_use_create(builder->context, inst, rhs); // Operand 1

  return &inst->result;
}

IRValueNode *
ir_builder_create_add(IRBuilder *builder, IRValueNode *lhs, IRValueNode *rhs)
{
  return builder_create_binary_op(builder, IR_OP_ADD, lhs, rhs);
}

IRValueNode *
ir_builder_create_sub(IRBuilder *builder, IRValueNode *lhs, IRValueNode *rhs)
{
  return builder_create_binary_op(builder, IR_OP_SUB, lhs, rhs);
}

IRValueNode *
ir_builder_create_icmp(IRBuilder *builder, IRICmpPredicate pred, IRValueNode *lhs, IRValueNode *rhs)
{
  assert(builder != NULL);
  assert(lhs != NULL && rhs != NULL);
  assert(lhs->type == rhs->type && "ICMP operands must have the same type");

  // 1. 获取结果类型 (关键: 总是 i1)
  IRType *result_type = ir_type_get_i1(builder->context);

  // 2. 调用你的内部工厂函数
  IRInstruction *inst = ir_instruction_create_internal(builder,
                                                       IR_OP_ICMP, // 新的 Opcode
                                                       result_type // 结果类型是 i1
  );

  if (!inst)
    return NULL; // OOM

  // 3. [!!] 设置 ICMP 特定的数据
  inst->as.icmp.predicate = pred;

  // 4. 创建 Use 边 (链接操作数)
  // (使用你已有的 ir_use_create)
  ir_use_create(builder->context, inst, lhs); // Operand 0
  ir_use_create(builder->context, inst, rhs); // Operand 1

  // 5. 返回指令的结果 (ValueNode)
  return &inst->result;
}

// --- API: 内存操作 ---

IRValueNode *
ir_builder_create_alloca(IRBuilder *builder, IRType *allocated_type)
{
  assert(allocated_type != NULL);
  IRContext *ctx = builder->context;

  // Alloca 的结果是一个指向 allocated_type 的指针
  IRType *ptr_type = ir_type_get_ptr(ctx, allocated_type);

  IRInstruction *inst = ir_instruction_create_internal(builder, IR_OP_ALLOCA, ptr_type);
  // (Alloca 0 个操作数)

  return &inst->result;
}

IRValueNode *
ir_builder_create_load(IRBuilder *builder, IRType *result_type, IRValueNode *ptr)
{
  assert(result_type != NULL);
  assert(ptr != NULL);
  assert(ptr->type->kind == IR_TYPE_PTR && "load operand must be a pointer");
  // (在真实编译器中, 还会检查 ptr->type->pointee_type == result_type)

  IRInstruction *inst = ir_instruction_create_internal(builder, IR_OP_LOAD, result_type);

  ir_use_create(builder->context, inst, ptr); // Operand 0 (指针)

  return &inst->result;
}

IRValueNode *
ir_builder_create_store(IRBuilder *builder, IRValueNode *val, IRValueNode *ptr)
{
  assert(val != NULL);
  assert(ptr != NULL);
  assert(ptr->type->kind == IR_TYPE_PTR && "store target must be a pointer");
  // (在真实编译器中, 还会检查 ptr->type->pointee_type == val->type)

  IRType *void_type = builder->context->type_void;
  IRInstruction *inst = ir_instruction_create_internal(builder, IR_OP_STORE, void_type);

  ir_use_create(builder->context, inst, val); // Operand 0 (要存储的值)
  ir_use_create(builder->context, inst, ptr); // Operand 1 (目标指针)

  return &inst->result;
}

// --- API: PHI 节点 ---

IRValueNode *
ir_builder_create_phi(IRBuilder *builder, IRType *type)
{
  assert(builder != NULL);
  assert(builder->insertion_point != NULL && "Builder insertion point is not set");
  assert(type != NULL && type->kind != IR_TYPE_VOID && "PHI type cannot be void");

  IRContext *ctx = builder->context;

  // 1. 从 ir_arena 分配 (类似 create_internal)
  IRInstruction *inst = BUMP_ALLOC_ZEROED(&ctx->ir_arena, IRInstruction);
  if (!inst)
    return NULL; // OOM

  // 2. 初始化基类 (IRValueNode)
  inst->result.kind = IR_KIND_INSTRUCTION;
  inst->result.type = type;
  list_init(&inst->result.uses);

  // 3. 初始化子类 (IRInstruction)
  inst->opcode = IR_OP_PHI; // [!!]
  inst->parent = builder->insertion_point;
  list_init(&inst->list_node);
  list_init(&inst->operands); // [!!] 初始为空

  // 4. 分配名字
  inst->result.name = builder_get_next_reg_name(builder);

  // 5. 关键: 插入到基本块的 *头部*
  list_add(&builder->insertion_point->instructions, &inst->list_node);

  return &inst->result;
}

void
ir_phi_add_incoming(IRValueNode *phi_node, IRValueNode *value, IRBasicBlock *incoming_bb)
{
  assert(phi_node != NULL && phi_node->kind == IR_KIND_INSTRUCTION);
  assert(value != NULL);
  assert(incoming_bb != NULL && incoming_bb->label_address.type->kind == IR_TYPE_LABEL);

  IRInstruction *inst = (IRInstruction *)phi_node;
  assert(inst->opcode == IR_OP_PHI && "Value is not a PHI node");
  assert(inst->result.type == value->type && "Incoming value type mismatch PHI type");

  // 1. 获取 Context
  // 我们需要 Context 来创建 'Use' 边
  // (仿照你的 ir_instruction_erase_from_parent 逻辑)
  assert(inst->parent != NULL && inst->parent->parent != NULL && inst->parent->parent->parent != NULL);
  IRContext *ctx = inst->parent->parent->parent->context;

  // 2. 添加两个操作数: value 和 basic_block
  ir_use_create(ctx, inst, value);
  ir_use_create(ctx, inst, &incoming_bb->label_address);
}
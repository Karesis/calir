#include "builder.h"

#include "ir/basicblock.h"
#include "ir/function.h"
#include "ir/instruction.h"
#include "ir/module.h"
#include "ir/type.h"
#include "ir/use.h"
#include "ir/value.h"

#include <assert.h> // for assert
#include <stdlib.h> // for malloc, free
#include <string.h> // for strdup

// --- 私有辅助函数 (Private Helpers) ---

/**
 * @brief (内部) 创建、初始化并插入一条新指令
 *
 * 这是所有 `ir_build_...` 函数的核心。
 * 它处理 malloc, 设置 opcode, 设置 parent, 初始化列表,
 * 并将新指令插入到 builder 的当前 insert_point。
 *
 * @param builder The IR builder.
 * @param opcode 指令的操作码
 * @param name 指令结果 Value 的名字 (e.g., "tmp1", 可为 NULL)
 * @param type 指令结果 Value 的类型 (e.g., i32, void)
 * @return 指向新创建的 IRInstruction 的指针
 */
static IRInstruction *
__inst_create_and_insert(IRBuilder *builder, IROpcode opcode, const char *name, IRType *type)
{
  assert(builder != NULL);
  assert(builder->insert_point != NULL && "Builder's insert_point is not set!");

  IRInstruction *inst = (IRInstruction *)malloc(sizeof(IRInstruction));
  if (!inst)
  {
    return NULL; // 内存分配失败
  }

  inst->opcode = opcode;
  inst->parent = builder->insert_point;

  // 初始化所有链表节点
  list_init(&inst->list_node);
  list_init(&inst->operands);

  // 初始化 'result' (IRValueNode 基类)
  inst->result.kind = IR_KIND_INSTRUCTION;
  inst->result.type = type;
  inst->result.name = (name) ? strdup(name) : NULL; // 复制名字
  list_init(&inst->result.uses);

  // 将指令插入到基本块的末尾
  list_add_tail(&builder->insert_point->instructions, &inst->list_node);

  return inst;
}

/**
 * @brief (内部) 为一条指令添加一个操作数 (Operand)
 *
 * 这会创建 `IRUse` 边，并将其正确链接到
 * `inst->operands` 和 `val->uses` 两个链表。
 *
 * @param inst 正在构建的指令 (User)
 * @param val 被使用的值 (Value)
 */
static void
__inst_add_operand(IRInstruction *inst, IRValueNode *val)
{
  assert(inst != NULL);
  assert(val != NULL && "Cannot add NULL as operand");

  // 1. 创建 Use 边
  IRUse *use = ir_use_create(inst, val);
  if (!use)
  {
    // 内存分配失败
    return;
  }

  // 2. 链接 Use 边 (自动添加到 inst->operands 和 val->uses)
  ir_use_link(use);
}

// --- 生命周期 (Lifecycle) ---

IRBuilder *
ir_builder_create()
{
  IRBuilder *builder = (IRBuilder *)malloc(sizeof(IRBuilder));
  if (!builder)
  {
    return NULL;
  }

  // 初始化上下文为空
  builder->insert_point = NULL;
  builder->current_function = NULL;
  builder->current_module = NULL;

  return builder;
}

void
ir_builder_destroy(IRBuilder *builder)
{
  if (builder)
  {
    free(builder);
  }
}

// --- 上下文管理 (Context Management) ---

void
ir_builder_set_insert_point(IRBuilder *builder, IRBasicBlock *bb)
{
  assert(builder != NULL);
  builder->insert_point = bb;

  // 当设置基本块时，自动更新函数和模块的上下文
  if (bb)
  {
    builder->current_function = bb->parent;
    if (builder->current_function)
    {
      builder->current_module = builder->current_function->parent;
    }
  }
  else
  {
    // 如果清除插入点，也清除上下文
    builder->current_function = NULL;
    builder->current_module = NULL;
  }
}

IRBasicBlock *
ir_builder_get_insert_block(IRBuilder *builder)
{
  assert(builder != NULL);
  return builder->insert_point;
}

IRFunction *
ir_builder_get_current_function(IRBuilder *builder)
{
  assert(builder != NULL);
  return builder->current_function;
}

IRModule *
ir_builder_get_current_module(IRBuilder *builder)
{
  assert(builder != NULL);
  return builder->current_module;
}

// --- 容器创建 (Container Creation) ---

IRBasicBlock *
ir_builder_create_basic_block(IRBuilder *builder, IRFunction *func, const char *name)
{
  assert(builder != NULL);
  assert(func != NULL);

  // 调用 basicblock.c 中的核心创建函数
  IRBasicBlock *bb = ir_basic_block_create(func, name);

  // (可选，但非常方便) 创建后立即设置插入点
  if (bb)
  {
    ir_builder_set_insert_point(builder, bb);
  }
  return bb;
}

IRFunction *
ir_builder_create_function(IRBuilder *builder, IRModule *mod, const char *name, IRType *ret_type)
{
  assert(builder != NULL);
  assert(mod != NULL);

  // 调用 function.c 中的核心创建函数
  IRFunction *func = ir_function_create(mod, name, ret_type);

  if (func)
  {
    // 更新 builder 上下文
    builder->current_module = mod;
    builder->current_function = func;
    builder->insert_point = NULL; // 新函数还没有基本块
  }
  return func;
}

// --- 指令创建 (Instruction Creation) ---

IRValueNode *
ir_build_ret(IRBuilder *builder, IRValueNode *val)
{
  // 'ret' 是一条 "void" 指令 (它不产生可被赋值的结果)
  IRType *ret_type = ir_type_get_void();
  IRInstruction *inst = __inst_create_and_insert(builder, IR_OP_RET, NULL, ret_type);

  if (val)
  {
    // 如果 val 不是 NULL, 说明是 'ret <type> <val>'
    // 添加 val 作为操作数
    __inst_add_operand(inst, val);

    // (可选) 'ret' 指令的 "type" 可以设为它返回的值的类型
    inst->result.type = val->type;
  }
  // else: 'ret void' (val is NULL), 没有操作数

  return &inst->result;
}

IRValueNode *
ir_build_br(IRBuilder *builder, IRBasicBlock *dest)
{
  assert(dest != NULL && "Branch destination cannot be NULL");

  // 'br' 也是 "void" 指令
  IRType *ret_type = ir_type_get_void();
  IRInstruction *inst = __inst_create_and_insert(builder, IR_OP_BR, NULL, ret_type);

  // 将目标基本块 (的标签 Value) 添加为操作数
  __inst_add_operand(inst, &dest->label_address);

  return &inst->result;
}

IRValueNode *
ir_build_add(IRBuilder *builder, IRValueNode *lhs, IRValueNode *rhs, const char *name)
{
  assert(lhs != NULL && rhs != NULL);
  assert(lhs->type == rhs->type && "Operands for 'add' must have the same type");

  // 'add' 的结果类型与操作数类型相同
  IRType *res_type = lhs->type;
  IRInstruction *inst = __inst_create_and_insert(builder, IR_OP_ADD, name, res_type);

  // 添加两个操作数
  __inst_add_operand(inst, lhs);
  __inst_add_operand(inst, rhs);

  return &inst->result;
}

IRValueNode *
ir_build_sub(IRBuilder *builder, IRValueNode *lhs, IRValueNode *rhs, const char *name)
{
  assert(lhs != NULL && rhs != NULL);
  assert(lhs->type == rhs->type && "Operands for 'sub' must have the same type");

  IRType *res_type = lhs->type;
  IRInstruction *inst = __inst_create_and_insert(builder, IR_OP_SUB, name, res_type);

  __inst_add_operand(inst, lhs);
  __inst_add_operand(inst, rhs);

  return &inst->result;
}

IRValueNode *
ir_build_alloca(IRBuilder *builder, IRType *type, const char *name)
{
  assert(type != NULL);

  // 'alloca' 指令的结果是一个 *指针*，指向被分配的类型
  IRType *ptr_type = ir_type_get_ptr(type);

  IRInstruction *inst = __inst_create_and_insert(builder, IR_OP_ALLOCA, name, ptr_type);

  // Alloca 指令没有 Value 操作数 (被分配的类型 'type'
  // 已经通过 ptr_type->pointee_type 编码在结果类型中了)

  return &inst->result;
}

IRValueNode *
ir_build_load(IRBuilder *builder, IRType *type, IRValueNode *ptr, const char *name)
{
  assert(type != NULL);
  assert(ptr != NULL);
  assert(ptr->type->kind == IR_TYPE_PTR && "Source for 'load' must be a pointer");
  assert(ptr->type->pointee_type == type && "Load type mismatch pointer's pointee type");

  // 'load' 的结果类型是 'type' (被加载的类型)
  IRInstruction *inst = __inst_create_and_insert(builder, IR_OP_LOAD, name, type);

  // 添加指针源作为操作数
  __inst_add_operand(inst, ptr);

  return &inst->result;
}

IRValueNode *
ir_build_store(IRBuilder *builder, IRValueNode *val, IRValueNode *ptr)
{
  assert(val != NULL);
  assert(ptr != NULL);
  assert(ptr->type->kind == IR_TYPE_PTR && "Destination for 'store' must be a pointer");
  assert(ptr->type->pointee_type == val->type && "Store value type mismatch pointer's pointee type");

  // 'store' 是一条 "void" 指令
  IRType *ret_type = ir_type_get_void();
  IRInstruction *inst = __inst_create_and_insert(builder, IR_OP_STORE, NULL, ret_type);

  // 操作数 0: 要存储的值
  __inst_add_operand(inst, val);
  // 操作数 1: 目标指针
  __inst_add_operand(inst, ptr);

  return &inst->result;
}
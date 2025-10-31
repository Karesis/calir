// include/ir/builder.h (重构版)
#ifndef IR_BUILDER_H
#define IR_BUILDER_H

#include "ir/instruction.h"
#include "ir/value.h"
#include <stddef.h>

// --- 前向声明 ---
typedef struct IRContext IRContext;
typedef struct IRBasicBlock IRBasicBlock;
typedef struct IRType IRType;

/**
 * @brief IR 构建器
 * (结构体定义保持不变)
 */
typedef struct IRBuilder
{
  IRContext *context;
  IRBasicBlock *insertion_point;
  size_t next_temp_reg_id;
} IRBuilder;

// --- 生命周期  ---
IRBuilder *ir_builder_create(IRContext *ctx);
void ir_builder_destroy(IRBuilder *builder);
void ir_builder_set_insertion_point(IRBuilder *builder, IRBasicBlock *bb);

// --- API: 终结者指令 (Terminators) ---
IRValueNode *ir_builder_create_ret(IRBuilder *builder, IRValueNode *val);
IRValueNode *ir_builder_create_br(IRBuilder *builder, IRValueNode *target_bb);
IRValueNode *ir_builder_create_cond_br(IRBuilder *builder, IRValueNode *cond, IRValueNode *true_bb,
                                       IRValueNode *false_bb);

// --- API: 二元运算 ---

/** * @brief 构建 'add <type> <lhs>, <rhs>'
 * @param name_hint [!!] (可选) 用于调试的名字 (e.g., "sum")
 */
IRValueNode *ir_builder_create_add(IRBuilder *builder, IRValueNode *lhs, IRValueNode *rhs, const char *name_hint);

/** * @brief 构建 'sub <type> <lhs>, <rhs>'
 * @param name_hint [!!] (可选)
 */
IRValueNode *ir_builder_create_sub(IRBuilder *builder, IRValueNode *lhs, IRValueNode *rhs, const char *name_hint);

/** * @brief 构建 'icmp <pred> <type> <op1>, <op2>‘
 * @param name_hint [!!] (可选) (e.g., "cmp")
 */
IRValueNode *ir_builder_create_icmp(IRBuilder *builder, IRICmpPredicate pred, IRValueNode *lhs, IRValueNode *rhs,
                                    const char *name_hint);

// --- API: 内存操作 ---

/** * @brief 构建 'alloca <type>'
 * @param name_hint [!!] (可选) (e.g., "ptr_x")
 */
IRValueNode *ir_builder_create_alloca(IRBuilder *builder, IRType *allocated_type, const char *name_hint);

/**
 * @brief 构建 'load <ptr>' (e.g., load %p)
 * [!!] API 签名已更改: 移除了 'result_type'
 *
 * @param builder Builder
 * @param ptr 要加载的指针 (e.g., <i32>*)
 * @param name_hint [!!] (可选) (e.g., "val_x")
 * @return 指向加载出的值的 ValueNode (e.g., i32)
 */
IRValueNode *ir_builder_create_load(IRBuilder *builder, IRValueNode *ptr, const char *name_hint);

/** @brief 构建 'store <val>, <ptr>'  */
IRValueNode *ir_builder_create_store(IRBuilder *builder, IRValueNode *val, IRValueNode *ptr);

/**
 * @brief 构建 'getelementptr <ty>, <ptr>, <idx...>'
 * @param name_hint [!!] (可选) (e.g., "elem_ptr")
 */
IRValueNode *ir_builder_create_gep(IRBuilder *builder, IRType *source_type, IRValueNode *base_ptr,
                                   IRValueNode **indices, size_t num_indices, bool inbounds, const char *name_hint);

// --- API: PHI 节点 ---

/**
 * @brief 构建 'phi <type>'
 * @param name_hint [!!] (可选) (e.g., "phi_res")
 */
IRValueNode *ir_builder_create_phi(IRBuilder *builder, IRType *type, const char *name_hint);

/** @brief 向 PHI 节点添加 [value, basic_block] 对 (保持不变) */
void ir_phi_add_incoming(IRValueNode *phi_node, IRValueNode *value, IRBasicBlock *incoming_bb);

// --- API: CALL 节点
/**
 * @brief 构建 'call <callee>, <arg1>, ...'
 *
 * @param callee_func 被调用的函数 (必须是 *指向函数类型* 的指针) [!!] 注释已更新
 * @param name_hint [!!] (可选) (e.g., "ret_val")
 */
IRValueNode *ir_builder_create_call(IRBuilder *builder, IRValueNode *callee_func, IRValueNode **args, size_t num_args,
                                    const char *name_hint);

#endif // IR_BUILDER_H
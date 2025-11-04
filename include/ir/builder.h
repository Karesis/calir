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

#ifndef IR_BUILDER_H
#define IR_BUILDER_H

#include "ir/instruction.h"
#include "ir/value.h"
#include <stddef.h>

typedef struct IRContext IRContext;
typedef struct IRBasicBlock IRBasicBlock;
typedef struct IRType IRType;

/**
 * @brief IR 构建器
 */
typedef struct IRBuilder
{
  IRContext *context;
  IRBasicBlock *insertion_point;
  size_t next_temp_reg_id;
} IRBuilder;

IRBuilder *ir_builder_create(IRContext *ctx);
void ir_builder_destroy(IRBuilder *builder);
void ir_builder_set_insertion_point(IRBuilder *builder, IRBasicBlock *bb);

IRValueNode *ir_builder_create_ret(IRBuilder *builder, IRValueNode *val);
IRValueNode *ir_builder_create_br(IRBuilder *builder, IRValueNode *target_bb);
IRValueNode *ir_builder_create_cond_br(IRBuilder *builder, IRValueNode *cond, IRValueNode *true_bb,
                                       IRValueNode *false_bb);

IRValueNode *ir_builder_create_mul(IRBuilder *builder, IRValueNode *lhs, IRValueNode *rhs, const char *name_hint);
IRValueNode *ir_builder_create_udiv(IRBuilder *builder, IRValueNode *lhs, IRValueNode *rhs, const char *name_hint);
IRValueNode *ir_builder_create_sdiv(IRBuilder *builder, IRValueNode *lhs, IRValueNode *rhs, const char *name_hint);
IRValueNode *ir_builder_create_urem(IRBuilder *builder, IRValueNode *lhs, IRValueNode *rhs, const char *name_hint);
IRValueNode *ir_builder_create_srem(IRBuilder *builder, IRValueNode *lhs, IRValueNode *rhs, const char *name_hint);

IRValueNode *ir_builder_create_fadd(IRBuilder *builder, IRValueNode *lhs, IRValueNode *rhs, const char *name_hint);
IRValueNode *ir_builder_create_fsub(IRBuilder *builder, IRValueNode *lhs, IRValueNode *rhs, const char *name_hint);
IRValueNode *ir_builder_create_fmul(IRBuilder *builder, IRValueNode *lhs, IRValueNode *rhs, const char *name_hint);
IRValueNode *ir_builder_create_fdiv(IRBuilder *builder, IRValueNode *lhs, IRValueNode *rhs, const char *name_hint);

IRValueNode *ir_builder_create_shl(IRBuilder *builder, IRValueNode *lhs, IRValueNode *rhs, const char *name_hint);
IRValueNode *ir_builder_create_lshr(IRBuilder *builder, IRValueNode *lhs, IRValueNode *rhs, const char *name_hint);
IRValueNode *ir_builder_create_ashr(IRBuilder *builder, IRValueNode *lhs, IRValueNode *rhs, const char *name_hint);
IRValueNode *ir_builder_create_and(IRBuilder *builder, IRValueNode *lhs, IRValueNode *rhs, const char *name_hint);
IRValueNode *ir_builder_create_or(IRBuilder *builder, IRValueNode *lhs, IRValueNode *rhs, const char *name_hint);
IRValueNode *ir_builder_create_xor(IRBuilder *builder, IRValueNode *lhs, IRValueNode *rhs, const char *name_hint);

/** * @brief 构建 'zext <val> to <dest_ty>'
 * @param val 要转换的值
 * @param dest_type 目标类型
 * @param name_hint (可选)
 */
IRValueNode *ir_builder_create_zext(IRBuilder *builder, IRValueNode *val, IRType *dest_type, const char *name_hint);
IRValueNode *ir_builder_create_trunc(IRBuilder *builder, IRValueNode *val, IRType *dest_type, const char *name_hint);
IRValueNode *ir_builder_create_sext(IRBuilder *builder, IRValueNode *val, IRType *dest_type, const char *name_hint);
IRValueNode *ir_builder_create_fptrunc(IRBuilder *builder, IRValueNode *val, IRType *dest_type, const char *name_hint);
IRValueNode *ir_builder_create_fpext(IRBuilder *builder, IRValueNode *val, IRType *dest_type, const char *name_hint);
IRValueNode *ir_builder_create_fptoui(IRBuilder *builder, IRValueNode *val, IRType *dest_type, const char *name_hint);
IRValueNode *ir_builder_create_fptosi(IRBuilder *builder, IRValueNode *val, IRType *dest_type, const char *name_hint);
IRValueNode *ir_builder_create_uitofp(IRBuilder *builder, IRValueNode *val, IRType *dest_type, const char *name_hint);
IRValueNode *ir_builder_create_sitofp(IRBuilder *builder, IRValueNode *val, IRType *dest_type, const char *name_hint);
IRValueNode *ir_builder_create_ptrtoint(IRBuilder *builder, IRValueNode *val, IRType *dest_type, const char *name_hint);
IRValueNode *ir_builder_create_inttoptr(IRBuilder *builder, IRValueNode *val, IRType *dest_type, const char *name_hint);
IRValueNode *ir_builder_create_bitcast(IRBuilder *builder, IRValueNode *val, IRType *dest_type, const char *name_hint);

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

/** @brief 构建 'fcmp <pred> <type> <op1>, <op2>‘ */
IRValueNode *ir_builder_create_fcmp(IRBuilder *builder, IRFCmpPredicate pred, IRValueNode *lhs, IRValueNode *rhs,
                                    const char *name_hint);

/**
 * @brief 构建 'select <cond>, <true_val>, <false_val>'
 * @param cond 条件 (必须是 i1)
 * @param true_val i1 为 true 时的值
 * @param false_val i1 为 false 时的值
 * @param name_hint (可选)
 */
IRValueNode *ir_builder_create_select(IRBuilder *builder, IRValueNode *cond, IRValueNode *true_val,
                                      IRValueNode *false_val, const char *name_hint);

/** * @brief 构建 'alloca <type>'
 * @param name_hint [!!] (可选) (e.g., "ptr_x")
 */
IRValueNode *ir_builder_create_alloca(IRBuilder *builder, IRType *allocated_type, const char *name_hint);

/**
 * @brief 构建 'load <ptr>' (e.g., load %p)
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

/**
 * @brief 构建 'phi <type>'
 * @param name_hint [!!] (可选) (e.g., "phi_res")
 */
IRValueNode *ir_builder_create_phi(IRBuilder *builder, IRType *type, const char *name_hint);

/** @brief 向 PHI 节点添加 [value, basic_block] 对 (保持不变) */
void ir_phi_add_incoming(IRValueNode *phi_node, IRValueNode *value, IRBasicBlock *incoming_bb);

/**
 * @brief 构建 'call <callee>, <arg1>, ...'
 *
 * @param callee_func 被调用的函数 (必须是 *指向函数类型* 的指针) [!!] 注释已更新
 * @param name_hint [!!] (可选) (e.g., "ret_val")
 */
IRValueNode *ir_builder_create_call(IRBuilder *builder, IRValueNode *callee_func, IRValueNode **args, size_t num_args,
                                    const char *name_hint);

/**
 * @brief 构建 'switch <cond>, default <default_bb> [...]'
 *
 * (不带 case)
 * @param cond 条件 (必须是整数类型)
 * @param default_bb 默认跳转的目标
 * @return 指向 switch 指令的 ValueNode
 */
IRValueNode *ir_builder_create_switch(IRBuilder *builder, IRValueNode *cond, IRValueNode *default_bb);

/**
 * @brief 向 Switch 指令添加 [const_val, target_bb] 对
 */
void ir_switch_add_case(IRValueNode *switch_inst, IRValueNode *const_val, IRValueNode *target_bb);

#endif
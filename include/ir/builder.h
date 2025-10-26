#ifndef IR_BUILDER_H
#define IR_BUILDER_H

#include "ir/instruction.h"
#include "ir/value.h" // 需要 IRValueNode
#include <stddef.h>   // 需要 size_t

// --- 前向声明 ---
typedef struct IRContext IRContext;
typedef struct IRBasicBlock IRBasicBlock;
typedef struct IRType IRType;

/**
 * @brief IR 构建器
 *
 * 这是一个辅助结构体，用于在 BasicBlock 中轻松创建和插入指令。
 * 它持有 Context 和当前的插入点。
 *
 * (Builder 结构体本身是 malloc/free 的，它只是一个临时的工具)
 */
typedef struct IRBuilder
{
  IRContext *context;
  IRBasicBlock *insertion_point;

  // 用于自动生成 %1, %2, %3...
  size_t next_temp_reg_id;

} IRBuilder;

// --- 生命周期 ---

/**
 * @brief 创建一个新的 IRBuilder
 * @param ctx Context (用于分配指令)
 * @return 指向新 Builder 的指针 (必须由 ir_builder_destroy 释放)
 */
IRBuilder *ir_builder_create(IRContext *ctx);

/**
 * @brief 销毁一个 IRBuilder
 * @param builder 要销毁的 Builder
 */
void ir_builder_destroy(IRBuilder *builder);

/**
 * @brief 设置 Builder 的当前插入点 (将在此 BB 末尾插入)
 * @param builder Builder
 * @param bb 目标基本块
 */
void ir_builder_set_insertion_point(IRBuilder *builder, IRBasicBlock *bb);

// --- API: 终结者指令 (Terminators) ---

/** @brief 构建 'ret <val>' 或 'ret void' */
IRValueNode *ir_builder_create_ret(IRBuilder *builder, IRValueNode *val);

/** @brief 构建 'br <target_bb>' (无条件跳转) */
IRValueNode *ir_builder_create_br(IRBuilder *builder, IRValueNode *target_bb);

// --- API: 二元运算 ---

/** @brief 构建 'add <type> <lhs>, <rhs>' */
IRValueNode *ir_builder_create_add(IRBuilder *builder, IRValueNode *lhs, IRValueNode *rhs);

/** @brief 构建 'sub <type> <lhs>, <rhs>' */
IRValueNode *ir_builder_create_sub(IRBuilder *builder, IRValueNode *lhs, IRValueNode *rhs);

/** @brief 构建 'icmp <pred> <type> <op1>, <op2>‘ */
IRValueNode *ir_builder_create_icmp(IRBuilder *builder, IRICmpPredicate pred, IRValueNode *lhs, IRValueNode *rhs);

// --- API: 内存操作 ---

/** @brief 构建 'alloca <type>' (e.g., alloca i32) */
IRValueNode *ir_builder_create_alloca(IRBuilder *builder, IRType *allocated_type);

/** @brief 构建 'load <type>, <ptr>' (e.g., load i32, ptr %p) */
IRValueNode *ir_builder_create_load(IRBuilder *builder, IRType *result_type, IRValueNode *ptr);

/** @brief 构建 'store <val>, <ptr>' (e.g., store i32 %a, ptr %p) */
IRValueNode *ir_builder_create_store(IRBuilder *builder, IRValueNode *val, IRValueNode *ptr);

#endif // IR_BUILDER_H
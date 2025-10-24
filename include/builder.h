// src/include/builder.h

#ifndef IR_BUILDER_H
#define IR_BUILDER_H

#include "ir/basicblock.h"
#include "ir/function.h"
#include "ir/instruction.h"
#include "ir/module.h"
#include "ir/type.h"
#include "ir/value.h"

/**
 * @brief IR 构建器
 *
 * 封装了创建 IR 对象的复杂逻辑，并管理
 * 当前的指令插入点。
 */
typedef struct IRBuilder
{
  /** 当前的指令插入点 (新指令会被插入到这个块的末尾) */
  IRBasicBlock *insert_point;

  /** (可选，但推荐) 追踪当前所在的函数 */
  IRFunction *current_function;

  /** (可选，但推荐) 追踪当前所在的模块 */
  IRModule *current_module;

} IRBuilder;

// --- 生命周期 ---

/** 创建一个新的 Builder */
IRBuilder *ir_builder_create();

/** 销毁一个 Builder */
void ir_builder_destroy(IRBuilder *builder);

// --- 上下文管理 API ---

/**
 * @brief 设置当前指令插入点
 * @param builder Builder
 * @param bb 新指令将被插入到此基本块的末尾
 */
void ir_builder_set_insert_point(IRBuilder *builder, IRBasicBlock *bb);

/**
 * @brief 获取当前的插入基本块
 */
IRBasicBlock *ir_builder_get_insert_block(IRBuilder *builder);

/**
 * @brief 获取当前插入点所在的函数
 * (通常由 set_insert_point 自动设置)
 */
IRFunction *ir_builder_get_current_function(IRBuilder *builder);

/**
 * @brief 获取当前插入点所在的模块
 * (通常由 set_insert_point 自动设置)
 */
IRModule *ir_builder_get_current_module(IRBuilder *builder);

// --- 容器创建 API (可选，但推荐) ---

/**
 * @brief 创建一个新的基本块，并将其插入到指定的函数末尾
 * * @param builder Builder (用于更新上下文)
 * @param func 要添加基本块的函数
 * @param name 基本块的标签名 (如 "entry", "if_then")
 * @return 创建的 IRBasicBlock
 */
IRBasicBlock *ir_builder_create_basic_block(IRBuilder *builder, IRFunction *func, const char *name);

/**
 * @brief 创建一个新函数，并将其插入到指定的模块末尾
 *
 * @param builder Builder (用于更新上下文)
 * @param mod 要添加函数的模块
 * @param name 函数名 (如 "main")
 * @param ret_type 函数返回类型
 * @return 创建的 IRFunction
 */
IRFunction *ir_builder_create_function(IRBuilder *builder, IRModule *mod, const char *name, IRType *ret_type);

// --- 指令创建 API (这才是 Builder 的核心) ---

/**
 * @brief 创建一条 'ret' (return) 指令
 * @param builder Builder
 * @param val 要返回的 Value (如果是 void, 传 NULL)
 * @return 指向新创建指令的 IRValueNode (即 inst->result)
 */
IRValueNode *ir_build_ret(IRBuilder *builder, IRValueNode *val);

/**
 * @brief 创建一条 'br' (unconditional branch) 指令
 * @param builder Builder
 * @param dest 目标基本块
 * @return 指向新创建指令的 IRValueNode (即 inst->result)
 */
IRValueNode *ir_build_br(IRBuilder *builder, IRBasicBlock *dest);

// --- 二元运算 ---

/**
 * @brief 创建一条 'add' 指令
 *
 * 指令会被插入到 builder 的当前 insert_point 的末尾。
 *
 * @param builder Builder
 * @param lhs 左操作数
 * @param rhs 右操作数
 * @param name 结果 ValueNode 的名字 (如 "tmp1")
 * @return 指向新创建指令的 IRValueNode (即 inst->result)
 */
IRValueNode *ir_build_add(IRBuilder *builder, IRValueNode *lhs, IRValueNode *rhs, const char *name);

/**
 * @brief 创建一条 'sub' 指令
 * @param builder Builder
 * @param lhs 左操作数
 * @param rhs 右操作数
 * @param name 结果 ValueNode 的名字 (如 "tmp2")
 * @return 指向新创建指令的 IRValueNode (即 inst->result)
 */
IRValueNode *ir_build_sub(IRBuilder *builder, IRValueNode *lhs, IRValueNode *rhs, const char *name);

// --- 内存操作 (你需要为此添加 IROpcode) ---

/**
 * @brief 创建一条 'alloca' 指令 (在栈上分配空间)
 *
 * @param builder Builder
 * @param type 要分配的类型 (例如 IR_TYPE_I32)
 * @param name 结果指针的名字 (如 "ptr_x")
 * @return 指向新创建指令的 IRValueNode (结果是一个指针类型)
 */
IRValueNode *ir_build_alloca(IRBuilder *builder, IRType *type, const char *name);

/**
 * @brief 创建一条 'load' 指令 (从内存读取)
 *
 * @param builder Builder
 * @param type 要加载的类型
 * @param ptr 指向要加载的内存的指针 (通常来自 alloca)
 * @param name 结果 ValueNode 的名字 (如 "loaded_val")
 * @return 指向新创建指令的 IRValueNode (结果是加载到的值)
 */
IRValueNode *ir_build_load(IRBuilder *builder, IRType *type, IRValueNode *ptr, const char *name);

/**
 * @brief 创建一条 'store' 指令 (写入内存)
 *
 * @param builder Builder
 * @param val 要存储的值
 * @param ptr 要存储到的内存地址 (通常来自 alloca)
 * @return 指向新创建指令的 IRValueNode (store 指令通常返回 void)
 */
IRValueNode *ir_build_store(IRBuilder *builder, IRValueNode *val, IRValueNode *ptr);

// ... 其他指令, 比如 ir_build_mul, ir_build_div, ir_build_call, ir_build_br_cond ...

#endif // IR_BUILDER_H
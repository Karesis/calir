#ifndef IR_BUILDER_H
#define IR_BUILDER_H

#include "ir/basicblock.h"
#include "ir/function.h"
#include "ir/instruction.h"
#include "ir/module.h"
#include "ir/type.h"
#include "ir/use.h"
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

  // 你也可以在这里添加一个 IRModule*
  // IRFunction* 来追踪当前上下文

} IRBuilder;

// --- API ---

/** 创建一个新的 Builder */
IRBuilder *ir_builder_create();

/** 销毁一个 Builder */
void ir_builder_destroy(IRBuilder *builder);

/**
 * @brief 设置当前指令插入点
 * @param builder Builder
 * @param bb 新指令将被插入到此基本块的末尾
 */
void ir_builder_set_insert_point(IRBuilder *builder, IRBasicBlock *bb);

// --- 指令创建 API (这才是 Builder 的核心) ---

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
 * @brief 创建一条 'ret' (return) 指令
 * @param builder Builder
 * @param val 要返回的 Value (如果是 void, 传 NULL)
 * @return 指向新创建指令的 IRValueNode (即 inst->result)
 */
IRValueNode *ir_build_ret(IRBuilder *builder, IRValueNode *val);

// ... 其他指令, 比如 ir_build_sub, ir_build_br, ir_build_alloca ...

#endif // IR_BUILDER_H
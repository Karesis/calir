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

#ifndef INSTRUCTION_H
#define INSTRUCTION_H

#include "ir/basicblock.h"
#include "ir/printer.h"
#include "ir/value.h"
#include "utils/id_list.h"

typedef enum
{

  IR_OP_RET,
  IR_OP_BR,
  IR_OP_COND_BR,
  IR_OP_SWITCH,

  IR_OP_ADD,
  IR_OP_SUB,
  IR_OP_MUL,
  IR_OP_UDIV,
  IR_OP_SDIV,
  IR_OP_UREM,
  IR_OP_SREM,

  IR_OP_FADD,
  IR_OP_FSUB,
  IR_OP_FMUL,
  IR_OP_FDIV,

  IR_OP_SHL,
  IR_OP_LSHR, /// 逻辑右移
  IR_OP_ASHR, /// 算术右移
  IR_OP_AND,
  IR_OP_OR,
  IR_OP_XOR,

  IR_OP_ALLOCA,
  IR_OP_LOAD,
  IR_OP_STORE,
  IR_OP_GEP,

  IR_OP_ICMP,
  IR_OP_FCMP,

  IR_OP_TRUNC,    /// 截断 (i64 -> i32)
  IR_OP_ZEXT,     /// 零扩展 (i32 -> i64)
  IR_OP_SEXT,     /// 符号扩展 (i32 -> i64)
  IR_OP_FPTRUNC,  /// 浮点截断 (f64 -> f32)
  IR_OP_FPEXT,    /// 浮点扩展 (f32 -> f64)
  IR_OP_FPTOUI,   /// 浮点 -> 无符号整数
  IR_OP_FPTOSI,   /// 浮点 -> 有符号整数
  IR_OP_UITOFP,   /// 无符号整数 -> 浮点
  IR_OP_SITOFP,   /// 有符号整数 -> 浮点
  IR_OP_PTRTOINT, /// 指针 -> 整数
  IR_OP_INTTOPTR, /// 整数 -> 指针
  IR_OP_BITCAST,  /// 位转换 (e.g., i32 -> f32)

  IR_OP_PHI,
  IR_OP_CALL

} IROpcode;

typedef enum
{
  IR_ICMP_EQ,  /// 等于
  IR_ICMP_NE,  /// 不等于
  IR_ICMP_UGT, /// 无符号大于
  IR_ICMP_UGE, /// 无符号大于等于
  IR_ICMP_ULT, /// 无符号小于
  IR_ICMP_ULE, /// 无符号小于等于
  IR_ICMP_SGT, /// 有符号大于
  IR_ICMP_SGE, /// 有符号大于等于
  IR_ICMP_SLT, /// 有符号小于
  IR_ICMP_SLE, /// 有符号小于等于
} IRICmpPredicate;

/**
 * @brief 浮点数比较 (FCMP) 的谓词
 * 'O' = Ordered (有序, 操作数非 NaN)
 * 'U' = Unordered (无序, 操作数至少有一个是 NaN)
 */
typedef enum
{

  /// (如果任一操作数为 NaN, 结果为 false)
  IR_FCMP_OEQ, /// Ordered and Equal (有序且等于)
  IR_FCMP_OGT, /// Ordered and Greater Than (有序且大于)
  IR_FCMP_OGE, /// Ordered and Greater Than or Equal (有序且大于等于)
  IR_FCMP_OLT, /// Ordered and Less Than (有序且小于)
  IR_FCMP_OLE, /// Ordered and Less Than or Equal (有序且小于等于)
  IR_FCMP_ONE, /// Ordered and Not Equal (有序且不等于)

  /// (如果任一/操作数为 NaN, 结果为 true)
  IR_FCMP_UEQ, /// Unordered or Equal (无序或等于)
  IR_FCMP_UGT, /// Unordered or Greater Than (无序或大于)
  IR_FCMP_UGE, /// Unordered or Greater Than or Equal (无序或大于等于)
  IR_FCMP_ULT, /// Unordered or Less Than (无序或小于)
  IR_FCMP_ULE, /// Unordered or Less Than or Equal (无序或小于等于)
  IR_FCMP_UNE, /// Unordered or Not Equal (无序或不等于)

  IR_FCMP_ORD, /// Ordered (有序, 检查两个操作数都不是 NaN)
  IR_FCMP_UNO, /// Unordered (无序, 检查至少一个操作数是 NaN)

  /// 始终为 true/false
  IR_FCMP_TRUE,
  IR_FCMP_FALSE,

} IRFCmpPredicate;

typedef struct
{
  IRValueNode result;
  IDList list_node;

  IROpcode opcode;
  IDList operands;
  IRBasicBlock *parent;
  union {

    struct
    {
      IRICmpPredicate predicate;
    } icmp;

    struct
    {
      IRFCmpPredicate predicate;
    } fcmp;

    struct
    {
      IRType *source_type;
      bool inbounds;
    } gep;
  } as;
} IRInstruction;

/**
 * @brief 从其父基本块中安全地擦除一条指令
 */
void ir_instruction_erase_from_parent(IRInstruction *inst);

/**
 * @brief 将单条指令的 IR 打印到 IRPrinter
 * @param inst 要打印的指令
 * @param p 打印机 (策略)
 */
void ir_instruction_dump(IRInstruction *inst, IRPrinter *p);

#endif
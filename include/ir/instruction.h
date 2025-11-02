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


  IR_OP_ADD,
  IR_OP_SUB,
  IR_OP_ICMP,


  IR_OP_ALLOCA,
  IR_OP_LOAD,
  IR_OP_STORE,
  IR_OP_PHI,
  IR_OP_GEP,
  IR_OP_CALL
} IROpcode;


typedef enum
{
  IR_ICMP_EQ,
  IR_ICMP_NE,
  IR_ICMP_UGT,
  IR_ICMP_UGE,
  IR_ICMP_ULT,
  IR_ICMP_ULE,
  IR_ICMP_SGT,
  IR_ICMP_SGE,
  IR_ICMP_SLT,
  IR_ICMP_SLE,
} IRICmpPredicate;


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
 * [!!] 签名已更改
 * @param inst 要打印的指令
 * @param p 打印机 (策略)
 */
void ir_instruction_dump(IRInstruction *inst, IRPrinter *p);

#endif
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


#include "ir/basicblock.h"
#include "ir/context.h"
#include "ir/function.h"
#include "ir/instruction.h"
#include "ir/printer.h"
#include "utils/bump.h"
#include "utils/id_list.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>



IRBasicBlock *
ir_basic_block_create(IRFunction *func, const char *name)
{
  assert(func != NULL && "Parent function cannot be NULL");
  IRContext *ctx = func->parent->context;


  IRBasicBlock *bb = (IRBasicBlock *)BUMP_ALLOC_ZEROED(&ctx->ir_arena, IRBasicBlock);
  if (!bb)
    return NULL;

  bb->parent = func;


  list_init(&bb->list_node);
  list_init(&bb->instructions);


  bb->label_address.kind = IR_KIND_BASIC_BLOCK;
  bb->label_address.name = ir_context_intern_str(ctx, name);
  list_init(&bb->label_address.uses);


  bb->label_address.type = ctx->type_label;

  return bb;
}

void
ir_function_append_basic_block(IRFunction *func, IRBasicBlock *bb)
{
  assert(func != NULL);
  assert(bb != NULL);
  assert(bb->parent == func && "Block being added to the wrong function?");

  list_add_tail(&func->basic_blocks, &bb->list_node);
}



/**
 * @brief [!!] 重构 [!!]
 * 将单个基本块的 IR 打印到 IRPrinter
 *
 * @param bb 要打印的基本块
 * @param p 打印机 (策略)
 */
void
ir_basic_block_dump(IRBasicBlock *bb, IRPrinter *p)
{
  if (!bb)
  {
    ir_print_str(p, "<null basicblock>\n");
    return;
  }


  ir_printf(p, "$%s:\n", bb->label_address.name);


  IDList *iter;
  list_for_each(&bb->instructions, iter)
  {
    IRInstruction *inst = list_entry(iter, IRInstruction, list_node);

    ir_print_str(p, "  ");


    ir_instruction_dump(inst, p);

    ir_print_str(p, "\n");
  }
}
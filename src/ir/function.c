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


#include "ir/function.h"
#include "ir/basicblock.h"
#include "ir/context.h"
#include "ir/module.h"
#include "ir/printer.h"
#include "ir/type.h"
#include "ir/value.h"
#include "utils/bump.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>



/**
 * @brief [内部] 创建一个函数参数 (IRArgument)
 * (由 ir_function_create 在内部调用)
 */
IRArgument *
ir_argument_create(IRFunction *func, IRType *type, const char *name)
{
  assert(func != NULL && "Parent function cannot be NULL");
  IRContext *ctx = func->parent->context;


  IRArgument *arg = BUMP_ALLOC_ZEROED(&ctx->ir_arena, IRArgument);
  if (!arg)
    return NULL;

  arg->parent = func;


  list_init(&arg->list_node);


  arg->value.kind = IR_KIND_ARGUMENT;
  if (name && name[0] != '\0')
  {

    arg->value.name = ir_context_intern_str(ctx, name);
  }
  else
  {

    arg->value.name = NULL;
  }
  arg->value.type = type;
  list_init(&arg->value.uses);


  list_add_tail(&func->arguments, &arg->list_node);

  return arg;
}



/**
 * @brief [新 API] 创建一个新函数 (在 Arena 中)
 */
IRFunction *
ir_function_create(IRModule *mod, const char *name, IRType *ret_type)
{
  assert(mod != NULL && ret_type != NULL);
  IRContext *ctx = mod->context;
  IRFunction *func = BUMP_ALLOC_ZEROED(&ctx->ir_arena, IRFunction);
  if (!func)
    return NULL;

  func->parent = mod;
  func->return_type = ret_type;
  func->function_type = NULL;

  list_init(&func->list_node);
  list_init(&func->arguments);
  list_init(&func->basic_blocks);


  func->entry_address.kind = IR_KIND_FUNCTION;
  func->entry_address.name = ir_context_intern_str(ctx, name);
  list_init(&func->entry_address.uses);


  func->entry_address.type = NULL;


  list_add_tail(&mod->functions, &func->list_node);
  return func;
}


void
ir_function_finalize_signature(IRFunction *func, bool is_variadic)
{
  assert(func != NULL && "Cannot finalize NULL function");
  assert(func->function_type == NULL && "Function signature already finalized");

  IRContext *ctx = func->parent->context;


  size_t num_args = 0;
  IDList *iter;
  list_for_each(&func->arguments, iter)
  {
    num_args++;
  }


  IRType **param_types = NULL;
  if (num_args > 0)
  {
    param_types = BUMP_ALLOC_SLICE(&ctx->permanent_arena, IRType *, num_args);
    if (!param_types)
      return;

    size_t i = 0;
    list_for_each(&func->arguments, iter)
    {
      IRArgument *arg = list_entry(iter, IRArgument, list_node);
      param_types[i++] = arg->value.type;
    }
  }


  IRType *func_type = ir_type_get_function(ctx, func->return_type, param_types, num_args, is_variadic);


  func->function_type = func_type;



  func->entry_address.type = ir_type_get_ptr(ctx, func_type);
}

/**
 * @brief ir_function_dump
 */

void
ir_function_dump(IRFunction *func, IRPrinter *p)
{
  if (!func)
  {
    ir_print_str(p, "<null function>\n");
    return;
  }


  bool is_declaration = list_empty(&func->basic_blocks);


  ir_print_str(p, is_declaration ? "declare " : "define ");


  ir_type_dump(func->return_type, p);


  ir_print_str(p, " ");
  ir_value_dump_name(&func->entry_address, p);
  ir_print_str(p, "(");


  IDList *arg_iter;
  int first_arg = 1;
  list_for_each(&func->arguments, arg_iter)
  {
    if (!first_arg)
    {
      ir_print_str(p, ", ");
    }
    IRArgument *arg = list_entry(arg_iter, IRArgument, list_node);


    ir_value_dump_with_type(&arg->value, p);

    first_arg = 0;
  }


  if (is_declaration)
  {
    ir_print_str(p, ")\n");
  }
  else
  {
    ir_print_str(p, ") {\n");


    IDList *bb_iter;
    list_for_each(&func->basic_blocks, bb_iter)
    {
      IRBasicBlock *bb = list_entry(bb_iter, IRBasicBlock, list_node);


      ir_basic_block_dump(bb, p);
    }

    ir_print_str(p, "}\n");
  }
}
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

#include "interpreter/interpreter.h"
#include "ir/context.h"
#include "ir/function.h"
#include "ir/module.h"
#include "ir/parser.h"
#include "utils/data_layout.h"
#include <stdio.h>
#include <string.h>

/// (From Quick Start 1)
const char *CIR_SOURCE = "module = \"parsed_module\"\n"
                         "\n"
                         "define i32 @add(%a: i32, %b: i32) {\n"
                         "$entry:\n"
                         "  %sum: i32 = add %a: i32, %b: i32\n"
                         "  ret %sum: i32\n"
                         "}\n";

int
main()
{
  IRContext *ctx = ir_context_create();
  DataLayout *dl = datalayout_create_host();
  Interpreter *interp = interpreter_create(dl);

  /// 1. Parse the module
  IRModule *mod = ir_parse_module(ctx, CIR_SOURCE);
  if (mod == NULL)
  {
    fprintf(stderr, "Failed to parse IR.\n");
    goto cleanup;
  }

  /// 2. Find the "@add" function
  IRFunction *add_func = NULL;
  IDList *it;
  list_for_each(&mod->functions, it)
  {
    IRFunction *f = list_entry(it, IRFunction, list_node);
    if (strcmp(f->entry_address.name, "add") == 0)
    {
      add_func = f;
      break;
    }
  }

  if (add_func == NULL)
  {
    fprintf(stderr, "Could not find function '@add' in module.\n");
    goto cleanup;
  }

  /// 3. Prepare arguments: 10 and 20
  RuntimeValue rt_a;
  rt_a.kind = RUNTIME_VAL_I32;
  rt_a.as.val_i32 = 10;

  RuntimeValue rt_b;
  rt_b.kind = RUNTIME_VAL_I32;
  rt_b.as.val_i32 = 20;

  RuntimeValue *args[] = {&rt_a, &rt_b};

  /// 4. Run the function
  RuntimeValue result;
  bool success = interpreter_run_function(interp, add_func, args, 2, &result);

  /// 5. Print the result
  if (success && result.kind == RUNTIME_VAL_I32)
  {
    printf("Result of @add(10, 20): %d\n", result.as.val_i32);
  }
  else
  {
    fprintf(stderr, "Interpreter run failed!\n");
  }

cleanup:
  interpreter_destroy(interp);
  datalayout_destroy(dl);
  ir_context_destroy(ctx);
  return 0;
}
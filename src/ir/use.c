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


#include "ir/use.h"
#include "ir/context.h"
#include "ir/instruction.h"
#include "ir/value.h"
#include "utils/bump.h"

#include <assert.h>

/**
 * @brief [内部] 创建一个 Use 边 (在 Arena 中)
 */
IRUse *
ir_use_create(IRContext *ctx, IRInstruction *user, IRValueNode *value)
{
  assert(ctx != NULL);
  assert(user != NULL);
  assert(value != NULL);


  IRUse *use = BUMP_ALLOC_ZEROED(&ctx->ir_arena, IRUse);
  if (!use)
    return NULL;

  use->value = value;
  use->user = user;


  list_add_tail(&user->operands, &use->user_node);


  list_add_tail(&value->uses, &use->value_node);

  return use;
}

/**
 * @brief [内部] 将 Use 边从两个链表中完全解开
 */
void
ir_use_unlink(IRUse *use)
{
  assert(use != NULL);
  list_del(&use->user_node);
  list_del(&use->value_node);
}

/**
 * @brief [内部] 更改 Use 边指向的 Value
 */
void
ir_use_set_value(IRUse *use, IRValueNode *new_val)
{
  assert(use != NULL);
  assert(new_val != NULL);


  list_del(&use->value_node);


  use->value = new_val;


  list_add_tail(&new_val->uses, &use->value_node);
}
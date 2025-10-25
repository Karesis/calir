#include "ir/use.h"
#include "context.h"
#include "ir/instruction.h" // 需要 IRInstruction->operands
#include "ir/value.h"       // 需要 IRValueNode->uses
#include "utils/bump.h"     // 需要 BUMP_ALLOC_ZEROED

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

  // 1. [修改] 从 ir_arena 分配
  IRUse *use = BUMP_ALLOC_ZEROED(&ctx->ir_arena, IRUse);
  if (!use)
    return NULL;

  use->value = value;
  use->user = user;

  // 2. 链接到 User (inst->operands)
  list_add_tail(&user->operands, &use->user_node);

  // 3. 链接到 Value (value->uses)
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

  // 1. 从 old_val->uses 链表中解开
  list_del(&use->value_node);

  // 2. 更新指针
  use->value = new_val;

  // 3. 链接到 new_val->uses 链表
  list_add_tail(&new_val->uses, &use->value_node);
}
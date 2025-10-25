#include "ir/basicblock.h"
#include "ir/context.h"
#include "ir/function.h"
#include "ir/instruction.h"
#include "utils/bump.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

// --- 生命周期 ---

IRBasicBlock *
ir_basic_block_create(IRFunction *func, const char *name)
{
  assert(func != NULL && "Parent function cannot be NULL");
  IRContext *ctx = func->parent->context; // [新] 从父级获取 Context

  // [修改] 从 ir_arena 分配
  IRBasicBlock *bb = (IRBasicBlock *)BUMP_ALLOC_ZEROED(&ctx->ir_arena, IRBasicBlock);
  if (!bb)
    return NULL;

  bb->parent = func;

  // [修改] 显式初始化链表
  list_init(&bb->list_node);
  list_init(&bb->instructions);

  // 初始化 IRValueNode 基类 (代表基本块标签地址)
  bb->label_address.kind = IR_KIND_BASIC_BLOCK;
  bb->label_address.name = ir_context_intern_str(ctx, name); // [修改] Intern 名字
  list_init(&bb->label_address.uses);                        // [修改] 显式初始化

  // [修改] 使用 Context 中的单例 'label' 类型
  bb->label_address.type = ctx->type_label;

  // 添加到父函数的基本块链表
  list_add_tail(&func->basic_blocks, &bb->list_node);

  return bb;
}

// --- 调试 ---

void
ir_basic_block_dump(IRBasicBlock *bb, FILE *stream)
{
  // (此函数的实现保持不变)
  if (!bb)
  {
    fprintf(stream, "<null basicblock>\n");
    return;
  }

  // 1. 打印标签 (e.g., "entry:")
  fprintf(stream, "%s:\n", bb->label_address.name);

  // 2. 打印所有指令 (带缩进)
  IDList *iter;
  list_for_each(&bb->instructions, iter)
  {
    IRInstruction *inst = list_entry(iter, IRInstruction, list_node);

    fprintf(stream, "  ");
    ir_instruction_dump(inst, stream); // <-- 依赖 instruction.c
    fprintf(stream, "\n");
  }
}
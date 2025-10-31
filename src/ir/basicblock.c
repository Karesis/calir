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

// --- 生命周期 ---

IRBasicBlock *
ir_basic_block_create(IRFunction *func, const char *name)
{
  assert(func != NULL && "Parent function cannot be NULL");
  IRContext *ctx = func->parent->context; // 从父级获取 Context

  // 从 ir_arena 分配
  IRBasicBlock *bb = (IRBasicBlock *)BUMP_ALLOC_ZEROED(&ctx->ir_arena, IRBasicBlock);
  if (!bb)
    return NULL;

  bb->parent = func;

  // 显式初始化链表
  list_init(&bb->list_node);
  list_init(&bb->instructions);

  // 初始化 IRValueNode 基类 (代表基本块标签地址)
  bb->label_address.kind = IR_KIND_BASIC_BLOCK;
  bb->label_address.name = ir_context_intern_str(ctx, name);
  list_init(&bb->label_address.uses);

  // 使用 Context 中的单例 'label' 类型
  bb->label_address.type = ctx->type_label;

  return bb;
}

void
ir_function_append_basic_block(IRFunction *func, IRBasicBlock *bb)
{
  assert(func != NULL);
  assert(bb != NULL);
  assert(bb->parent == func && "Block being added to the wrong function?");
  // TODO: 检查 bb 是否已在链表中?
  list_add_tail(&func->basic_blocks, &bb->list_node);
}

// --- 调试 ---

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
    ir_print_str(p, "<null basicblock>\n"); // [!!] 已更改
    return;
  }

  // 1. 打印标签 (e.g., "$entry:")
  ir_printf(p, "$%s:\n", bb->label_address.name); // [!!] 已更改

  // 2. 打印所有指令 (带缩进)
  IDList *iter;
  list_for_each(&bb->instructions, iter)
  {
    IRInstruction *inst = list_entry(iter, IRInstruction, list_node);

    ir_print_str(p, "  "); // [!!] 已更改 (打印缩进)

    // [!!] 调用 (尚未重构的) ir_instruction_dump
    ir_instruction_dump(inst, p);

    ir_print_str(p, "\n"); // [!!] 已更改 (打印换行)
  }
}
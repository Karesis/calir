// src/ir/basicblock.c

#include "ir/basicblock.h"
#include "ir/function.h"
#include "ir/instruction.h" // <-- 需要 (用于 destroy 和 dump)

#include <assert.h>
#include <stdlib.h>
#include <string.h>

// --- 生命周期 ---

IRBasicBlock *
ir_basic_block_create(IRFunction *func, const char *name)
{
  assert(func != NULL && "Parent function cannot be NULL");

  IRBasicBlock *bb = (IRBasicBlock *)malloc(sizeof(IRBasicBlock));
  if (!bb)
    return NULL;

  bb->parent = func;
  list_init(&bb->list_node);
  list_init(&bb->instructions);

  // 初始化 IRValueNode 基类 (代表基本块标签地址)
  bb->label_address.kind = IR_KIND_BASIC_BLOCK;
  bb->label_address.name = strdup(name);
  list_init(&bb->label_address.uses);

  // TODO: 标签 (label) 的类型
  // 在 LLVM 中, 'label' 是一种特殊的类型。
  // 你的类型系统目前没有 "label" 类型。
  // 暂时设为 NULL，但将来你可能需要添加 IR_TYPE_LABEL。
  bb->label_address.type = NULL;

  // 添加到父函数的基本块链表
  list_add_tail(&func->basic_blocks, &bb->list_node);

  return bb;
}

void
ir_basic_block_destroy(IRBasicBlock *bb)
{
  if (!bb)
    return;

  // 1. 从父函数链表中移除
  list_del(&bb->list_node);

  // 2. 销毁所有指令
  // (需要 ir_instruction_destroy 的实现)
  IDList *iter, *temp;
  list_for_each_safe(&bb->instructions, iter, temp)
  {
    IRInstruction *inst = list_entry(iter, IRInstruction, list_node);
    ir_instruction_destroy(inst); // <-- 依赖 instruction.c
  }

  // 3. 释放标签名 (在 ValueNode 中)
  free(bb->label_address.name);

  // 4. 释放基本块自身
  free(bb);
}

// --- 调试 ---

void
ir_basic_block_dump(IRBasicBlock *bb, FILE *stream)
{
  if (!bb)
  {
    fprintf(stream, "<null basicblock>\n");
    return;
  }

  // 1. 打印标签 (e.g., "entry:")
  // (标签本身不缩进)
  fprintf(stream, "%s:\n", bb->label_address.name);

  // 2. 打印所有指令 (带缩进)
  // (需要 ir_instruction_dump 的实现)
  IDList *iter;
  list_for_each(&bb->instructions, iter)
  {
    IRInstruction *inst = list_entry(iter, IRInstruction, list_node);

    // 打印缩进
    fprintf(stream, "  ");

    // 打印指令
    ir_instruction_dump(inst, stream); // <-- 依赖 instruction.c

    fprintf(stream, "\n");
  }
}
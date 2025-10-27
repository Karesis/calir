// src/analysis/cfg.c
#include "analysis/cfg.h"
#include "ir/instruction.h" // 用于 opcode
#include "ir/use.h"
#include "ir/value.h"      // 用于 get_operand
#include "utils/id_list.h" // 你的 id_list API

// -----------------------------------------------------------
// 辅助函数 (从你的 verifier.c 中借鉴)
// -----------------------------------------------------------

/**
 * @brief 获取指令的第 N 个操作数 (ValueNode)
 */
static inline IRValueNode *
get_operand(IRInstruction *inst, int index)
{
  int i = 0;
  IDList *iter_node;
  list_for_each(&inst->operands, iter_node)
  {
    if (i == index)
    {
      IRUse *use = list_entry(iter_node, IRUse, user_node);
      return use->value;
    }
    i++;
  }
  return NULL; // 索引越界
}

// -----------------------------------------------------------
// 内部实现
// -----------------------------------------------------------

/**
 * @brief [内部] 向 CFG 添加一条边 (使用你的 bump.h API)
 */
static void
cfg_add_edge(FunctionCFG *cfg, CFGNode *from, CFGNode *to)
{
  if (!from || !to)
    return;

  // 1. 创建 succ 边 (from -> to)
  // 使用 BUMP_ALLOC 宏来分配 CFGEdge
  CFGEdge *succ_edge = BUMP_ALLOC(&cfg->arena, CFGEdge);
  succ_edge->node = to;
  list_add_tail(&from->successors, &succ_edge->list_node);

  // 2. 创建 pred 边 (from -> to)
  CFGEdge *pred_edge = BUMP_ALLOC(&cfg->arena, CFGEdge);
  pred_edge->node = from;
  list_add_tail(&to->predecessors, &pred_edge->list_node);
}

// -----------------------------------------------------------
// 公共 API
// -----------------------------------------------------------

FunctionCFG *
cfg_build(IRFunction *func, Bump *arena)
{
  // 1. 分配 FunctionCFG 结构体本身 (使用 BUMP_ALLOC_ZEROED 确保指针为 NULL)
  FunctionCFG *cfg = BUMP_ALLOC_ZEROED(arena, FunctionCFG);
  cfg->func = func;

  // 2. 初始化 CFG 自己的内部竞技场 (使用 bump_init，假设最小对齐为 1)
  bump_init(&cfg->arena);

  // 3. 初始化 PtrHashMap (假设 API 如此)
  cfg->block_to_node_map = ptrmap_create();

  // --- PASS 1: 创建节点并构建映射 ---

  // 统计有多少个块
  cfg->num_nodes = 0;
  IDList *bb_it;
  list_for_each(&func->basic_blocks, bb_it)
  {
    cfg->num_nodes++;
  }

  if (cfg->num_nodes == 0)
  {
    cfg->nodes = NULL;
    cfg->entry_node = NULL;
    // (我们不需要销毁 arena 或 hashmap，因为空函数
    //  在 verifier 中会直接返回，最后统一销毁)
    return cfg;
  }

  // 在 CFG 自己的竞技场上分配所有 CFGNode 数组
  // 我们 *不* 使用 ZEROED，因为我们需要手动调用 list_init
  cfg->nodes = BUMP_ALLOC_SLICE(&cfg->arena, CFGNode, cfg->num_nodes);

  int current_id = 0;
  list_for_each(&func->basic_blocks, bb_it)
  {
    IRBasicBlock *bb = list_entry(bb_it, IRBasicBlock, list_node);
    CFGNode *node = &cfg->nodes[current_id];

    // 初始化节点
    node->block = bb;
    node->id = current_id;
    // [!] 必须手动调用 list_init
    list_init(&node->successors);
    list_init(&node->predecessors);

    // 设置入口节点 (假设入口块总是第一个)
    if (current_id == 0)
    {
      cfg->entry_node = node;
    }

    // 填充 hashmap
    ptrmap_put(cfg->block_to_node_map, bb, node);

    current_id++;
  }

  // --- PASS 2: 扫描终结者，添加边 ---

  for (int i = 0; i < cfg->num_nodes; i++)
  {
    CFGNode *current_node = &cfg->nodes[i];
    IRBasicBlock *bb = current_node->block;

    if (list_empty(&bb->instructions))
    {
      continue; // Verifier 会捕获这个
    }

    IRInstruction *term = list_entry(bb->instructions.prev, IRInstruction, list_node);

    switch (term->opcode)
    {
    case IR_OP_BR: // 无条件跳转
    {
      IRValueNode *target_val = get_operand(term, 0);
      // [!] 你的 IRValueNode* (label) 就是 IRBasicBlock*
      IRBasicBlock *target_bb = (IRBasicBlock *)target_val;
      CFGNode *target_node = cfg_get_node(cfg, target_bb);

      cfg_add_edge(cfg, current_node, target_node);
      break;
    }

    case IR_OP_COND_BR: // 条件跳转
    {
      // 操作数 1: true_bb
      IRValueNode *true_val = get_operand(term, 1);
      IRBasicBlock *true_bb = (IRBasicBlock *)true_val;
      CFGNode *true_node = cfg_get_node(cfg, true_bb);

      cfg_add_edge(cfg, current_node, true_node);

      // 操作数 2: false_bb
      IRValueNode *false_val = get_operand(term, 2);
      IRBasicBlock *false_bb = (IRBasicBlock *)false_val;
      CFGNode *false_node = cfg_get_node(cfg, false_bb);

      if (true_node != false_node)
      {
        cfg_add_edge(cfg, current_node, false_node);
      }
      break;
    }

    case IR_OP_RET:
    default: {
      // 'ret' 没有后继节点
      // 其他非终结者指令 Verifier 会处理
      break;
    }
    }
  }

  return cfg;
}

void
cfg_destroy(FunctionCFG *cfg)
{
  if (!cfg)
    return;

  // 1. 销毁 PtrHashMap (假设 API 如此)
  ptrmap_destroy(cfg->block_to_node_map);

  // 2. 销毁用于 CFG 节点、边 和 数组 的内部竞技场
  bump_destroy(&cfg->arena);

  // 3. FunctionCFG 结构体本身由外部竞技场管理，无需 free
}
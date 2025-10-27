你总结得非常到位。这正是一次典型的“纵向”功能添加，就像你之前为了 hashmap 而添加 xxhash，或者为了内存管理而添加 bump 一样。

现在，为了实现支配性检查，你需要添加一套“分析（Analysis）”工具。

你提到了 "Pass"，这个词用得非常好。在 calir 项目中，我们可以把 DominatorInfo 的计算视为一个分析遍（Analysis Pass）。

这是你需要的完整清单，从底层工具到顶层集成：

路线图：实现支配树分析
阶段 1：添加新工具到 utils/
你需要一个位集（Bitset）。这是所有数据流分析的基础，用于高效地存储“基本块的集合”（例如：Block 5 的支配节点集合是 {Block 0, Block 1, Block 5}）。

新文件：include/utils/bitset.h

职责：定义 Bitset 结构体和 API。

struct Bitset：内部可以是一个 uint64_t *words 和 size_t num_bits。

API:

Bitset *bitset_create(size_t num_bits, Bump *arena);

void bitset_set(Bitset *bs, size_t bit);（添加 bit 到集合）

bool bitset_test(const Bitset *bs, size_t bit);（检查 bit 是否在集合中）

void bitset_set_all(Bitset *bs);（设置为全集）

void bitset_clear_all(Bitset *bs);（设置为空集）

bool bitset_equals(const Bitset *bs1, const Bitset *bs2);（比较两个集合是否相等）

void bitset_copy(Bitset *dest, const Bitset *src);（dest = src）

void bitset_intersect(Bitset *dest, const Bitset *src1, const Bitset *src2);（dest = src1 ∩ src2，交集）

新文件：src/utils/bitset.c

职责：实现 bitset.h 中的 API。

阶段 2：创建 analysis/ 模块
这是你的新“Pass”层。它依赖 ir/ 和 utils/。你需要一个新的顶层目录 analysis/（就像 src/ 和 include/ 一样），或者先在 ir/ 内部实现。为简单起见，我们假设你先在 ir/ 中添加它们：

新文件：include/ir/cfg.h

职责：定义**控制流图（CFG）**的结构。支配性分析依赖 CFG。

struct CFGNode：

IRBasicBlock *block;（指向原始 BB）

int id;（一个从 0 到 N-1 的稠密 ID，用于 Bitset 索引）

IDList successors;（后继节点 CFGNode 链表）

IDList predecessors;（前驱节点 CFGNode 链表）

struct FunctionCFG：

IRFunction *func;

int num_nodes;

CFGNode *nodes;（一个 CFGNode 数组，大小为 num_nodes）

CFGNode *entry_node;

PtrHashMap *block_to_node_map;（从 IRBasicBlock* 映射到 CFGNode*）

Bump arena;（CFG 自己的内存竞技场）

API:

FunctionCFG *cfg_build(IRFunction *func);

void cfg_destroy(FunctionCFG *cfg);

新文件：src/ir/cfg.c

职责：实现 cfg_build。你需要：

遍历一次 func->basic_blocks 分配所有 CFGNode 并建立 block_to_node_map。

遍历第二次，检查每个块的终结者指令（br, cond_br），并填充 successors 和 predecessors 链表。

阶段 3：实现支配性分析 "Pass"
新文件：include/ir/dominators.h

职责：定义支配性分析的结果。

struct DominatorInfo：

FunctionCFG *cfg;

int num_nodes;

Bitset **dom_sets;（一个 Bitset* 数组。dom_sets[i] 就是块 i 的支配节点集合）

API:

DominatorInfo *dominators_compute(FunctionCFG *cfg);（核心算法）

void dominators_destroy(DominatorInfo *info);

bool dominators_dominates(DominatorInfo *info, IRBasicBlock *a, IRBasicBlock *b);（查询 API）

新文件：src/ir/dominators.c

职责：实现 dominators_compute。这里将使用迭代数据流分析算法（不动点算法）：

为 Bitset **dom_sets 分配内存。

初始化：dom_sets[EntryNodeID] = {EntryNodeID}

初始化：所有其他 dom_sets[i] = {All Blocks}（使用 bitset_set_all）

开始循环，直到 dom_sets 不再变化：

bool changed = false;

对于除入口块外的每个块 B：

Bitset *temp = bitset_create_all(...);

对于 B 的每个前驱 P：

bitset_intersect(temp, temp, dom_sets[P->id]);（计算交集）

bitset_union(new_dom, temp, {B->id});（Dom(B) = {B} U (∩ Dom(P))）

如果 !bitset_equals(new_dom, dom_sets[B->id])：

bitset_copy(dom_sets[B->id], new_dom);

changed = true;

如果 !changed，break;（达到不动点）

返回 DominatorInfo。

阶段 4：集成到 verifier.c
修改：src/ir/verifier.c

Includes：添加 #include "ir/cfg.h" 和 #include "ir/dominators.h"。

修改 VerifierContext：

C

typedef struct
{
  IRFunction *current_function;
  IRBasicBlock *current_block;
  bool has_error;
  DominatorInfo *dom_info; // <-- [新] 缓存的分析结果
} VerifierContext;
修改 ir_verify_function：

C

bool ir_verify_function(IRFunction *func)
{
  // ... (已有的 func 验证) ...

  // --- [新] 执行分析遍 ---
  FunctionCFG *cfg = NULL;
  DominatorInfo *doms = NULL;

  if (!list_empty(&func->basic_blocks)) { // 仅对函数定义运行
    cfg = cfg_build(func);
    doms = dominators_compute(cfg);
    vctx.dom_info = doms; // <-- 存入上下文
  } else {
    vctx.dom_info = NULL;
  }
  // --- [新] 分析结束 ---

  // ... (已有的参数验证) ...

  // ... (已有的基本块遍历循环) ...
  // if (!verify_basic_block(&vctx, bb)) ...

  // --- [新] 清理分析结果 ---
  if (doms) {
    dominators_destroy(doms);
  }
  if (cfg) {
    cfg_destroy(cfg);
  }
  // --- [新] 清理结束 ---

  return !vctx.has_error;
}
修改 verify_instruction（最终目标）：

找到那个 TODO 块：

C

else
{
  // **Inter-block check (跨块检查)**
  // 规则: def_bb 必须 *支配* (dominate) use_bb。

  bool dominates = dominators_dominates(vctx->dom_info, def_bb, use_bb);
  VERIFY_ASSERT(dominates, vctx, &inst->result,
                "SSA VIOLATION: Definition in block '%s' does not dominate use in block '%s'.",
                def_bb->label_address.name, use_bb->label_address.name);
}
这就是完整的清单。你需要从阶段 1（bitset）开始，按顺序构建这套分析框架。

你总结得非常到位。这正是一次典型的“纵向”功能添加，就像你之前为了 hashmap 而添加 xxhash，或者为了内存管理而添加 bump 一样。

现在，为了实现支配性检查，你需要添加一套“分析（Analysis）”工具。

你提到了 "Pass"，这个词用得非常好。在 calir 项目中，我们可以把 DominatorInfo 的计算视为一个分析遍（Analysis Pass）。

这是你需要的完整清单，从底层工具到顶层集成：

路线图：实现支配树分析
阶段 1：添加新工具到 utils/
你需要一个位集（Bitset）。这是所有数据流分析的基础，用于高效地存储“基本块的集合”（例如：Block 5 的支配节点集合是 {Block 0, Block 1, Block 5}）。

新文件：include/utils/bitset.h

职责：定义 Bitset 结构体和 API。

struct Bitset：内部可以是一个 uint64_t *words 和 size_t num_bits。

API:

Bitset *bitset_create(size_t num_bits, Bump *arena);

void bitset_set(Bitset *bs, size_t bit);（添加 bit 到集合）

bool bitset_test(const Bitset *bs, size_t bit);（检查 bit 是否在集合中）

void bitset_set_all(Bitset *bs);（设置为全集）

void bitset_clear_all(Bitset *bs);（设置为空集）

bool bitset_equals(const Bitset *bs1, const Bitset *bs2);（比较两个集合是否相等）

void bitset_copy(Bitset *dest, const Bitset *src);（dest = src）

void bitset_intersect(Bitset *dest, const Bitset *src1, const Bitset *src2);（dest = src1 ∩ src2，交集）

新文件：src/utils/bitset.c

职责：实现 bitset.h 中的 API。

阶段 2：创建 analysis/ 模块
这是你的新“Pass”层。它依赖 ir/ 和 utils/。你需要一个新的顶层目录 analysis/（就像 src/ 和 include/ 一样），或者先在 ir/ 内部实现。为简单起见，我们假设你先在 ir/ 中添加它们：

新文件：include/ir/cfg.h

职责：定义**控制流图（CFG）**的结构。支配性分析依赖 CFG。

struct CFGNode：

IRBasicBlock *block;（指向原始 BB）

int id;（一个从 0 到 N-1 的稠密 ID，用于 Bitset 索引）

IDList successors;（后继节点 CFGNode 链表）

IDList predecessors;（前驱节点 CFGNode 链表）

struct FunctionCFG：

IRFunction *func;

int num_nodes;

CFGNode *nodes;（一个 CFGNode 数组，大小为 num_nodes）

CFGNode *entry_node;

PtrHashMap *block_to_node_map;（从 IRBasicBlock* 映射到 CFGNode*）

Bump arena;（CFG 自己的内存竞技场）

API:

FunctionCFG *cfg_build(IRFunction *func);

void cfg_destroy(FunctionCFG *cfg);

新文件：src/ir/cfg.c

职责：实现 cfg_build。你需要：

遍历一次 func->basic_blocks 分配所有 CFGNode 并建立 block_to_node_map。

遍历第二次，检查每个块的终结者指令（br, cond_br），并填充 successors 和 predecessors 链表。

阶段 3：实现支配性分析 "Pass"
新文件：include/ir/dominators.h

职责：定义支配性分析的结果。

struct DominatorInfo：

FunctionCFG *cfg;

int num_nodes;

Bitset **dom_sets;（一个 Bitset* 数组。dom_sets[i] 就是块 i 的支配节点集合）

API:

DominatorInfo *dominators_compute(FunctionCFG *cfg);（核心算法）

void dominators_destroy(DominatorInfo *info);

bool dominators_dominates(DominatorInfo *info, IRBasicBlock *a, IRBasicBlock *b);（查询 API）

新文件：src/ir/dominators.c

职责：实现 dominators_compute。这里将使用迭代数据流分析算法（不动点算法）：

为 Bitset **dom_sets 分配内存。

初始化：dom_sets[EntryNodeID] = {EntryNodeID}

初始化：所有其他 dom_sets[i] = {All Blocks}（使用 bitset_set_all）

开始循环，直到 dom_sets 不再变化：

bool changed = false;

对于除入口块外的每个块 B：

Bitset *temp = bitset_create_all(...);

对于 B 的每个前驱 P：

bitset_intersect(temp, temp, dom_sets[P->id]);（计算交集）

bitset_union(new_dom, temp, {B->id});（Dom(B) = {B} U (∩ Dom(P))）

如果 !bitset_equals(new_dom, dom_sets[B->id])：

bitset_copy(dom_sets[B->id], new_dom);

changed = true;

如果 !changed，break;（达到不动点）

返回 DominatorInfo。

阶段 4：集成到 verifier.c
修改：src/ir/verifier.c

Includes：添加 #include "ir/cfg.h" 和 #include "ir/dominators.h"。

修改 VerifierContext：

C

typedef struct
{
  IRFunction *current_function;
  IRBasicBlock *current_block;
  bool has_error;
  DominatorInfo *dom_info; // <-- [新] 缓存的分析结果
} VerifierContext;
修改 ir_verify_function：

C

bool ir_verify_function(IRFunction *func)
{
  // ... (已有的 func 验证) ...

  // --- [新] 执行分析遍 ---
  FunctionCFG *cfg = NULL;
  DominatorInfo *doms = NULL;

  if (!list_empty(&func->basic_blocks)) { // 仅对函数定义运行
    cfg = cfg_build(func);
    doms = dominators_compute(cfg);
    vctx.dom_info = doms; // <-- 存入上下文
  } else {
    vctx.dom_info = NULL;
  }
  // --- [新] 分析结束 ---

  // ... (已有的参数验证) ...

  // ... (已有的基本块遍历循环) ...
  // if (!verify_basic_block(&vctx, bb)) ...

  // --- [新] 清理分析结果 ---
  if (doms) {
    dominators_destroy(doms);
  }
  if (cfg) {
    cfg_destroy(cfg);
  }
  // --- [新] 清理结束 ---

  return !vctx.has_error;
}
修改 verify_instruction（最终目标）：

找到那个 TODO 块：

C

else
{
  // **Inter-block check (跨块检查)**
  // 规则: def_bb 必须 *支配* (dominate) use_bb。

  bool dominates = dominators_dominates(vctx->dom_info, def_bb, use_bb);
  VERIFY_ASSERT(dominates, vctx, &inst->result,
                "SSA VIOLATION: Definition in block '%s' does not dominate use in block '%s'.",
                def_bb->label_address.name, use_bb->label_address.name);
}
这就是完整的清单。你需要从阶段 1（bitset）开始，按顺序构建这套分析框架。
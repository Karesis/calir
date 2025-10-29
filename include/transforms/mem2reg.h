#ifndef CALIR_TRANSFORMS_MEM2REG_H
#define CALIR_TRANSFORMS_MEM2REG_H

#include "analysis/dom_frontier.h"
#include "analysis/dom_tree.h"
#include "ir/function.h"

/**
 * @brief 执行 "Promote Memory to Register" (mem2reg) 变换。
 *
 * 这个 pass 会寻找入口块中的 'alloca' 指令，并尝试将它们
 * 提升到 SSA 寄存器中，用 PHI 节点替换 'load' 和 'store'。
 *
 * @param func 要变换的函数。
 * @param dt 此函数的支配树。
 * @param df 此函数的支配边界。
 * @return 如果 IR 被修改则返回 true，否则返回 false。
 */
bool ir_transform_mem2reg_run(IRFunction *func, DominatorTree *dt, DominanceFrontier *df);

#endif // CALIR_TRANSFORMS_MEM2REG_H
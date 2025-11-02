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

#endif
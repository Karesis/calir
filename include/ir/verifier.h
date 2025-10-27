#ifndef IR_VERIFIER_H
#define IR_VERIFIER_H

#include "ir/function.h"
#include "ir/module.h"
#include <stdbool.h>

/**
 * @brief 验证一个完整的 IRModule.
 *
 * 遍历模块中的所有函数、基本块和指令，检查其是否符合 IR 规则。
 * 如果发现错误，将向 stderr 打印详细的错误信息。
 *
 * @param mod 要验证的模块。
 * @return 如果模块是良构的 (well-formed)，返回 true；否则返回 false。
 */
bool ir_verify_module(IRModule *mod);

/**
 * @brief 验证一个单独的 IRFunction.
 *
 * 遍历函数中的所有基本块和指令，检查其是否符合 IR 规则。
 * 如果发现错误，将向 stderr 打印详细的错误信息。
 *
 * @param func 要验证的函数。
 * @return 如果函数是良构的 (well-formed)，返回 true；否则返回 false。
 */
bool ir_verify_function(IRFunction *func);

#endif // IR_VERIFIER_H
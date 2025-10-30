#ifndef CALIR_IR_LEXER_H
#define CALIR_IR_LEXER_H

#include "ir/context.h" // Lexer 需要 Context 来做字符串驻留
#include <stddef.h>
#include <stdint.h>

// 前向声明
typedef struct IRContext IRContext;

/**
 * @brief 词法单元 (Token) 的类型
 */
typedef enum
{
  TK_ILLEGAL, // 非法字符
  TK_EOF,     // 文件结束

  // --- 标识符和字面量 ---
  TK_IDENT,        // e.g., define, i32, add, my_label
  TK_GLOBAL_IDENT, // e.g., @my_global, @main
  TK_LOCAL_IDENT,  // e.g., %x, %0, %entry

  // 字面量
  TK_INTEGER_LITERAL, // e.g., '123', '-42'
  TK_FLOAT_LITERAL,   // [新] e.g., '1.23', '-0.5'
  TK_STRING_LITERAL,  // [新] e.g., '"Hello\n"'

  // --- 标点符号 ---
  TK_EQ,        // =
  TK_COMMA,     // ,
  TK_COLON,     // :
  TK_LBRACE,    // {
  TK_RBRACE,    // }
  TK_LBRACKET,  // [
  TK_RBRACKET,  // ]
  TK_LPAREN,    // (
  TK_RPAREN,    // )
  TK_SEMICOLON, // ; (用于注释)

  // (注意：'ptr', 'add', 'i32' 都会被解析为 TK_IDENT，
  //  *Parser* 将负责识别它们是关键字、类型还是普通标识符)

} TokenType;

/**
 * @brief 词法单元 (Token) 结构体
 *
 * 存储类型和（如果适用）解析好的值。
 */
typedef struct Token
{
  TokenType type;
  int line; // Token 所在的行号 (用于报错)

  union {
    // 用于 TK_IDENT, TK_GLOBAL_IDENT, TK_LOCAL_IDENT, TK_STRING_LITERAL
    // 指针指向 Context->permanent_arena 中唯一的字符串
    const char *ident_val;

    // 用于 TK_INTEGER_LITERAL
    int64_t int_val;

    // 用于 TK_FLOAT_LITERAL
    double float_val;
  } as;
} Token;

/**
 * @brief 词法分析器 (Lexer)
 *
 * 这是一个 "Peeking" Lexer，它总是有一个 'current_token'。
 */
typedef struct Lexer
{
  IRContext *context;       // 用于字符串驻留
  const char *buffer_start; // 输入的 .cir 文件的完整内容
  const char *ptr;          // 当前解析到的字符位置
  int line;                 // 当前行号

  Token current; // 当前的 Token
  Token peek;    // LL(2) 预读
} Lexer;

// --- Lexer API ---

/**
 * @brief 初始化 Lexer
 * @param lexer Lexer 实例
 * @param buffer 包含 .cir 文件内容的 C 字符串 (必须以 '\0' 结尾)
 * @param ctx IR 上下文 (用于字符串驻留)
 */
void ir_lexer_init(Lexer *lexer, const char *buffer, IRContext *ctx);

/**
 * @brief "吃掉" 当前 Token，并让 Lexer 解析下一个 Token。
 *
 * 将 peek_token 移动到 current_token，并解析新的 peek_token。
 * (为了简单起见，我们先实现一个 LL(1) 的，只用 current_token)
 */
void ir_lexer_next(Lexer *lexer);

/**
 * @brief 获取*当前* Token (K=1)
 *
 * @param lexer Lexer 实例
 * @return const Token*
 */
const Token *ir_lexer_current_token(const Lexer *lexer);

/**
 * @brief 预读*下一个* Token (K=2)
 *
 * @param lexer Lexer 实例
 * @return const Token*
 */
const Token *ir_lexer_peek_token(const Lexer *lexer);

/**
 * @brief [辅助] "吃掉" 当前 Token，如果它匹配预期类型。
 * @param lexer Lexer
 * @param expected 预期的 Token 类型
 * @return 如果 Token 匹配则返回 true，否则返回 false (并设置 TK_ILLEGAL)
 */
bool ir_lexer_eat(Lexer *lexer, TokenType expected);

#endif // CALIR_IR_LEXER_H
#ifndef TYPE_H
#define TYPE_H

// 类型系统
typedef enum
{
  IR_TYPE_VOID,
  IR_TYPE_I32,
  IR_TYPE_PTR,
} IRTypeKind;

typedef struct
{
  IRTypeKind kind;
  IRType *pointee_type; // 用于指针类型
} IRType;

/**
 * @brief 获取 'void' 类型 (单例)
 */
IRType *ir_type_get_void();

/**
 * @brief 获取 'i32' 类型 (单例)
 */
IRType *ir_type_get_i32();

/**
 * @brief 创建/获取一个指针类型
 *
 * @param pointee_type 指针所指向的类型
 * @return 指向 'ptr' 类型的指针
 */
IRType *ir_type_get_ptr(IRType *pointee_type);

/**
 * @brief 销毁一个类型
 *
 * !! 注意: 仅用于销毁 'ptr' 类型 (它们是 malloc 出来的)。
 * 不要对 'void' 或 'i32' 单例使用此函数。
 */
void ir_type_destroy(IRType *type);

// --- 调试 ---

/**
 * @brief 将类型打印到字符串缓冲区
 * (这是我们之前在 instruction.c 等文件中使用的 temp_type_to_string 的正式版本)
 *
 * @param type 要打印的类型
 * @param buffer 目标缓冲区
 * @param size 缓冲区大小
 */
void ir_type_to_string(IRType *type, char *buffer, size_t size);

/**
 * @brief 将类型打印到流
 * @param type 要打印的类型
 * @param stream 输出流
 */
void ir_type_dump(IRType *type, FILE *stream);

#endif
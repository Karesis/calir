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

#endif
#include "ir/context.h"
#include "ir/constant.h" // 需要 ir_constant_create_*
#include "ir/type.h"     // 需要 ir_type_create_*
#include "utils/bump.h"
#include "utils/hashmap.h" // 包含所有 hashmap 头文件
#include <assert.h>
#include <stdlib.h> // for malloc, free
#include <string.h> // for strlen, memcpy

// 定义哈希表的初始容量
#define INITIAL_CACHE_CAPACITY 64

/*
 * =================================================================
 * --- 私有辅助函数 ---
 * =================================================================
 */

/**
 * @brief 初始化所有单例类型 (i32, void, ...)
 * @return true 成功, false OOM
 */
static bool
ir_context_init_singleton_types(IRContext *ctx)
{
  // 注意：这些函数 (ir_type_create_primitive)
  // 会在 ctx->permanent_arena 中分配
#define CREATE_TYPE(ty_kind, field)                                                                                    \
  do                                                                                                                   \
  {                                                                                                                    \
    ctx->field = ir_type_create_primitive(ctx, ty_kind);                                                               \
    if (!ctx->field)                                                                                                   \
      return false;                                                                                                    \
  } while (0)

  CREATE_TYPE(IR_TYPE_VOID, type_void);
  CREATE_TYPE(IR_TYPE_I1, type_i1);
  CREATE_TYPE(IR_TYPE_I8, type_i8);
  CREATE_TYPE(IR_TYPE_I16, type_i16);
  CREATE_TYPE(IR_TYPE_I32, type_i32);
  CREATE_TYPE(IR_TYPE_I64, type_i64);
  CREATE_TYPE(IR_TYPE_F32, type_f32);
  CREATE_TYPE(IR_TYPE_F64, type_f64);
  CREATE_TYPE(IR_TYPE_LABEL, type_label);

#undef CREATE_TYPE
  return true;
}

/**
 * @brief 初始化所有单例常量 (true, false)
 * @return true 成功, false OOM
 */
static bool
ir_context_init_singleton_constants(IRContext *ctx)
{
  // 注意：这些函数 (ir_constant_create_int)
  // 会在 ctx->permanent_arena 中分配

  // 创建 const i1 true
  ctx->const_i1_true = ir_constant_create_int(ctx, ctx->type_i1, 1);
  if (!ctx->const_i1_true)
    return false;

  // 创建 const i1 false
  ctx->const_i1_false = ir_constant_create_int(ctx, ctx->type_i1, 0);
  if (!ctx->const_i1_false)
    return false;

  return true;
}

/**
 * @brief 初始化所有哈希表缓存
 * @return true 成功, false OOM
 */
static bool
ir_context_init_caches(IRContext *ctx)
{
  Bump *arena = &ctx->permanent_arena;

#define CREATE_CACHE(create_func, field)                                                                               \
  do                                                                                                                   \
  {                                                                                                                    \
    ctx->field = create_func(arena, INITIAL_CACHE_CAPACITY);                                                           \
    if (!ctx->field)                                                                                                   \
      return false;                                                                                                    \
  } while (0)

  // Type Caches
  CREATE_CACHE(ptr_hashmap_create, pointer_type_cache);

  // Constant Caches
  CREATE_CACHE(i8_hashmap_create, i8_constant_cache);
  CREATE_CACHE(i16_hashmap_create, i16_constant_cache);
  CREATE_CACHE(i32_hashmap_create, i32_constant_cache);
  CREATE_CACHE(i64_hashmap_create, i64_constant_cache);
  CREATE_CACHE(f32_hashmap_create, f32_constant_cache);
  CREATE_CACHE(f64_hashmap_create, f64_constant_cache);
  CREATE_CACHE(ptr_hashmap_create, undef_constant_cache);

  // String Interning
  CREATE_CACHE(str_hashmap_create, string_intern_cache);

#undef CREATE_CACHE
  return true;
}

/*
 * =================================================================
 * --- 公共 API: 生命周期 ---
 * =================================================================
 */

IRContext *
ir_context_create(void)
{
  // 1. 分配 Context 结构体本身 (使用标准 malloc)
  IRContext *ctx = (IRContext *)malloc(sizeof(IRContext));
  if (!ctx)
    return NULL;

  // 2. 初始化两个 Arenas
  // (我们使用 bump_init, 因为 Arena 嵌入在 struct 中)
  bump_init(&ctx->permanent_arena); // 默认对齐
  bump_init(&ctx->ir_arena);

  // 3. 初始化所有缓存 (使用 permanent_arena)
  if (!ir_context_init_caches(ctx))
  {
    // OOM during cache creation
    bump_destroy(&ctx->permanent_arena);
    bump_destroy(&ctx->ir_arena);
    free(ctx);
    return NULL;
  }

  // 4. 初始化单例类型 (使用 permanent_arena)
  if (!ir_context_init_singleton_types(ctx))
  {
    // OOM during type creation
    bump_destroy(&ctx->permanent_arena); // Caches, Types 都会被释放
    bump_destroy(&ctx->ir_arena);
    free(ctx);
    return NULL;
  }

  // 5. 初始化单例常量 (使用 permanent_arena)
  if (!ir_context_init_singleton_constants(ctx))
  {
    // OOM during constant creation
    bump_destroy(&ctx->permanent_arena); // Caches, Types, Constants 都会被释放
    bump_destroy(&ctx->ir_arena);
    free(ctx);
    return NULL;
  }

  return ctx;
}

void
ir_context_destroy(IRContext *ctx)
{
  if (!ctx)
    return;

  // 1. 销毁 Arenas
  // (这也释放了所有 Arenas 中的对象: types, constants, hashmaps, modules, ...)
  bump_destroy(&ctx->permanent_arena);
  bump_destroy(&ctx->ir_arena);

  // 2. 释放 Context 结构体本身
  free(ctx);
}

void
ir_context_reset_ir_arena(IRContext *ctx)
{
  assert(ctx != NULL);
  // 重置临时 Arena，销毁所有 Modules, Functions, Instructions
  // 保留 permanent_arena (Types, Constants, Caches)
  bump_reset(&ctx->ir_arena);
}

/*
 * =================================================================
 * --- 公共 API: 类型 (Types) ---
 * =================================================================
 */

// 单例 Getter
IRType *
ir_type_get_void(IRContext *ctx)
{
  return ctx->type_void;
}
IRType *
ir_type_get_i1(IRContext *ctx)
{
  return ctx->type_i1;
}
IRType *
ir_type_get_i8(IRContext *ctx)
{
  return ctx->type_i8;
}
IRType *
ir_type_get_i16(IRContext *ctx)
{
  return ctx->type_i16;
}
IRType *
ir_type_get_i32(IRContext *ctx)
{
  return ctx->type_i32;
}
IRType *
ir_type_get_i64(IRContext *ctx)
{
  return ctx->type_i64;
}
IRType *
ir_type_get_f32(IRContext *ctx)
{
  return ctx->type_f32;
}
IRType *
ir_type_get_f64(IRContext *ctx)
{
  return ctx->type_f64;
}

/**
 * @brief 创建/获取一个指针类型 (唯一化)
 */
IRType *
ir_type_get_ptr(IRContext *ctx, IRType *pointee_type)
{
  assert(ctx != NULL);
  assert(pointee_type != NULL);

  // 1. 检查缓存
  // (void*) 在这里用作通用指针 key
  void *cached = ptr_hashmap_get(ctx->pointer_type_cache, (void *)pointee_type);
  if (cached)
  {
    return (IRType *)cached;
  }

  // 2. 未命中？创建新类型
  IRType *new_ptr_type = ir_type_create_ptr(ctx, pointee_type);
  if (!new_ptr_type)
  {
    // OOM
    return NULL;
  }

  // 3. 存入缓存
  // (注意：如果 OOM，put 可能失败，但在 Arena 分配器中，
  // 我们假设如果 create 成功，hashmap put 也会成功，因为它也来自同一个 Arena)
  ptr_hashmap_put(ctx->pointer_type_cache, (void *)pointee_type, (void *)new_ptr_type);

  return new_ptr_type;
}

/*
 * =================================================================
 * --- 公共 API: 常量 (Constants) ---
 * =================================================================
 */

/**
 * @brief 获取一个 'undef' 常量 (唯一化)
 */
IRValueNode *
ir_constant_get_undef(IRContext *ctx, IRType *type)
{
  assert(ctx != NULL);
  assert(type != NULL);

  // 1. 检查缓存
  void *cached = ptr_hashmap_get(ctx->undef_constant_cache, (void *)type);
  if (cached)
  {
    return (IRValueNode *)cached;
  }

  // 2. 未命中？创建新常量
  IRValueNode *new_undef = ir_constant_create_undef(ctx, type);
  if (!new_undef)
    return NULL; // OOM

  // 3. 存入缓存
  ptr_hashmap_put(ctx->undef_constant_cache, (void *)type, (void *)new_undef);

  return new_undef;
}

/**
 * @brief 获取一个 i1 (bool) 整数常量 (唯一化)
 */
IRValueNode *
ir_constant_get_i1(IRContext *ctx, bool value)
{
  assert(ctx != NULL);
  return value ? ctx->const_i1_true : ctx->const_i1_false;
}

// 宏，用于实现 i8, i16, i32, i64 的 get_int
#define DEFINE_GET_INT_CONSTANT(BITS, C_TYPE, HASHMAP_TYPE, HASHMAP_FIELD, GET_FUNC)                                   \
  IRValueNode *ir_constant_get_##BITS(IRContext *ctx, C_TYPE value)                                                    \
  {                                                                                                                    \
    assert(ctx != NULL);                                                                                               \
    /* 1. 检查缓存 */                                                                                                  \
    void *cached = GET_FUNC(ctx->HASHMAP_FIELD, value);                                                                \
    if (cached)                                                                                                        \
    {                                                                                                                  \
      return (IRValueNode *)cached;                                                                                    \
    }                                                                                                                  \
    /* 2. 未命中？创建新常量 */                                                                                        \
    /* (注意：我们将 C_TYPE 提升为 int64_t 传给构造函数) */                                                            \
    IRValueNode *new_const = ir_constant_create_int(ctx, ctx->type_##BITS, (int64_t)value);                            \
    if (!new_const)                                                                                                    \
      return NULL; /* OOM */                                                                                           \
    /* 3. 存入缓存 */                                                                                                  \
    HASHMAP_TYPE##_put(ctx->HASHMAP_FIELD, value, (void *)new_const);                                                  \
    return new_const;                                                                                                  \
  }

// 使用宏定义所有整数常量 getter
DEFINE_GET_INT_CONSTANT(i8, int8_t, i8_hashmap, i8_constant_cache, i8_hashmap_get)
DEFINE_GET_INT_CONSTANT(i16, int16_t, i16_hashmap, i16_constant_cache, i16_hashmap_get)
DEFINE_GET_INT_CONSTANT(i32, int32_t, i32_hashmap, i32_constant_cache, i32_hashmap_get)
DEFINE_GET_INT_CONSTANT(i64, int64_t, i64_hashmap, i64_constant_cache, i64_hashmap_get)

// 宏，用于实现 f32, f64 的 get_float
#define DEFINE_GET_FLOAT_CONSTANT(BITS, C_TYPE, HASHMAP_TYPE, HASHMAP_FIELD, GET_FUNC)                                 \
  IRValueNode *ir_constant_get_##BITS(IRContext *ctx, C_TYPE value)                                                    \
  {                                                                                                                    \
    assert(ctx != NULL);                                                                                               \
    /* 1. 检查缓存 */                                                                                                  \
    void *cached = GET_FUNC(ctx->HASHMAP_FIELD, value);                                                                \
    if (cached)                                                                                                        \
    {                                                                                                                  \
      return (IRValueNode *)cached;                                                                                    \
    }                                                                                                                  \
    /* 2. 未命中？创建新常量 */                                                                                        \
    /* (注意：我们将 C_TYPE 提升为 double 传给构造函数) */                                                             \
    IRValueNode *new_const = ir_constant_create_float(ctx, ctx->type_##BITS, (double)value);                           \
    if (!new_const)                                                                                                    \
      return NULL; /* OOM */                                                                                           \
    /* 3. 存入缓存 */                                                                                                  \
    HASHMAP_TYPE##_put(ctx->HASHMAP_FIELD, value, (void *)new_const);                                                  \
    return new_const;                                                                                                  \
  }

// 使用宏定义所有浮点常量 getter
DEFINE_GET_FLOAT_CONSTANT(f32, float, f32_hashmap, f32_constant_cache, f32_hashmap_get)
DEFINE_GET_FLOAT_CONSTANT(f64, double, f64_hashmap, f64_constant_cache, f64_hashmap_get)

/*
 * =================================================================
 * --- 公共 API: 字符串 (String Interning) ---
 * =================================================================
 */

/**
 * @brief 唯一化一个字符串切片
 */
const char *
ir_context_intern_str_slice(IRContext *ctx, const char *str, size_t len)
{
  assert(ctx != NULL);
  assert(str != NULL || len == 0);

  // 1. 检查缓存
  void *cached = str_hashmap_get(ctx->string_intern_cache, str, len);
  if (cached)
  {
    // 命中！返回那个唯一的、已在 Arena 中的指针
    return (const char *)cached;
  }

  // 2. 未命中？在 permanent_arena 中创建 *唯一* 副本
  //    (我们分配 len + 1 来保证 NUL 终止符)
  char *new_str = (char *)bump_alloc(&ctx->permanent_arena, len + 1, 1);
  if (!new_str)
    return NULL; // OOM

  memcpy(new_str, str, len);
  new_str[len] = '\0'; // 确保 C 字符串兼容性

  // 3. 存入缓存 (使用高效的新 API)
  //    Key:   new_str (指针, 长度 len)
  //    Value: new_str (指针)
  //    Hashmap 不会再复制 Key，它会直接存储 new_str 指针。
  bool put_ok = str_hashmap_put_preallocated_key(ctx->string_intern_cache,
                                                 new_str,        // Key (指针)
                                                 len,            // Key (长度)
                                                 (void *)new_str // Value (指针)
  );

  if (!put_ok)
  {
    // 在极少数情况下 (例如 hashmap grow 失败)
    // 尽管 Arena 分配器通常能避免这种情况
    return NULL;
  }

  // 返回我们新创建的、唯一的字符串指针
  return (const char *)new_str;
}

/**
 * @brief 唯一化一个字符串 (以 '\0' 结尾)
 */
const char *
ir_context_intern_str(IRContext *ctx, const char *str)
{
  assert(str != NULL);
  size_t len = strlen(str);
  return ir_context_intern_str_slice(ctx, str, len);
}

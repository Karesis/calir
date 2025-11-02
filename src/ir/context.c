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


#include "ir/context.h"
#include "ir/constant.h"
#include "ir/type.h"
#include "utils/bump.h"
#include "utils/hashmap.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>


#define INITIAL_CACHE_CAPACITY 64

/*
 * =================================================================
 * --- 私有辅助函数 ---
 * =================================================================
 */


/**
 * @brief [内部] 匿名结构体缓存的 "Key" 结构体。
 *
 * *重要*: 存储在 GenericHashMap 中的 Key 必须是*永久*的。
 * 我们在 permanent_arena 中分配这个结构体。
 */
typedef struct
{
  IRType **members;
  size_t count;
} AnonStructKey;


#define XXH_INLINE_ALL
#include "utils/xxhash.h"

/**
 * @brief [内部] GenericHashMap 的哈希函数。
 * @param key 指向 AnonStructKey 的 (const void*) 指针。
 */
static uint64_t
anon_struct_hash_fn(const void *key)
{
  const AnonStructKey *k = (const AnonStructKey *)key;

  return XXH3_64bits(k->members, k->count * sizeof(IRType *));
}

/**
 * @brief [内部] GenericHashMap 的比较函数。
 * @param key1 指向 AnonStructKey 1 的 (const void*) 指针。
 * @param key2 指向 AnonStructKey 2 的 (const void*) 指针。
 */
static bool
anon_struct_equal_fn(const void *key1, const void *key2)
{
  const AnonStructKey *k1 = (const AnonStructKey *)key1;
  const AnonStructKey *k2 = (const AnonStructKey *)key2;

  if (k1->count != k2->count)
  {
    return false;
  }


  return memcmp(k1->members, k2->members, k1->count * sizeof(IRType *)) == 0;
}

/**
 * @brief [内部] 函数类型缓存的 "Key" 结构体。
 * (同 AnonStructKey, 必须永久存储)
 */
typedef struct
{
  IRType *return_type;
  IRType **param_types;
  size_t param_count;
  bool is_variadic;
} FunctionTypeKey;

/**
 * @brief [内部] GenericHashMap 的哈希函数 (用于函数类型)。
 */
static uint64_t
function_type_hash_fn(const void *key)
{
  const FunctionTypeKey *k = (const FunctionTypeKey *)key;


  uint64_t hash = XXH3_64bits(k->param_types, k->param_count * sizeof(IRType *));


  hash = XXH3_64bits_withSeed(&k->return_type, sizeof(k->return_type), hash);
  hash = XXH3_64bits_withSeed(&k->is_variadic, sizeof(k->is_variadic), hash);

  return hash;
}

/**
 * @brief [内部] GenericHashMap 的比较函数 (用于函数类型)。
 */
static bool
function_type_equal_fn(const void *key1, const void *key2)
{
  const FunctionTypeKey *k1 = (const FunctionTypeKey *)key1;
  const FunctionTypeKey *k2 = (const FunctionTypeKey *)key2;


  if (k1->return_type != k2->return_type || k1->param_count != k2->param_count || k1->is_variadic != k2->is_variadic)
  {
    return false;
  }


  if (k1->param_count == 0)
  {
    return true;
  }


  return memcmp(k1->param_types, k2->param_types, k1->param_count * sizeof(IRType *)) == 0;
}

/**
 * @brief 初始化所有单例类型 (i32, void, ...)
 * @return true 成功, false OOM
 */
static bool
ir_context_init_singleton_types(IRContext *ctx)
{


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




  ctx->const_i1_true = ir_constant_create_int(ctx, ctx->type_i1, 1);
  if (!ctx->const_i1_true)
    return false;


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


  CREATE_CACHE(ptr_hashmap_create, pointer_type_cache);
  CREATE_CACHE(ptr_hashmap_create, array_type_cache);
  CREATE_CACHE(str_hashmap_create, named_struct_cache);


  ctx->anon_struct_cache = generic_hashmap_create(&ctx->permanent_arena, INITIAL_CACHE_CAPACITY,
                                                  anon_struct_hash_fn,
                                                  anon_struct_equal_fn
  );
  if (!ctx->anon_struct_cache)
    return false;


  ctx->function_type_cache = generic_hashmap_create(&ctx->permanent_arena, INITIAL_CACHE_CAPACITY,
                                                    function_type_hash_fn,
                                                    function_type_equal_fn
  );
  if (!ctx->function_type_cache)
    return false;


  CREATE_CACHE(i8_hashmap_create, i8_constant_cache);
  CREATE_CACHE(i16_hashmap_create, i16_constant_cache);
  CREATE_CACHE(i32_hashmap_create, i32_constant_cache);
  CREATE_CACHE(i64_hashmap_create, i64_constant_cache);
  CREATE_CACHE(f32_hashmap_create, f32_constant_cache);
  CREATE_CACHE(f64_hashmap_create, f64_constant_cache);
  CREATE_CACHE(ptr_hashmap_create, undef_constant_cache);


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

  IRContext *ctx = (IRContext *)malloc(sizeof(IRContext));
  if (!ctx)
    return NULL;



  bump_init(&ctx->permanent_arena);
  bump_init(&ctx->ir_arena);


  if (!ir_context_init_caches(ctx))
  {

    bump_destroy(&ctx->permanent_arena);
    bump_destroy(&ctx->ir_arena);
    free(ctx);
    return NULL;
  }


  if (!ir_context_init_singleton_types(ctx))
  {

    bump_destroy(&ctx->permanent_arena);
    bump_destroy(&ctx->ir_arena);
    free(ctx);
    return NULL;
  }


  if (!ir_context_init_singleton_constants(ctx))
  {

    bump_destroy(&ctx->permanent_arena);
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



  bump_destroy(&ctx->permanent_arena);
  bump_destroy(&ctx->ir_arena);


  free(ctx);
}

void
ir_context_reset_ir_arena(IRContext *ctx)
{
  assert(ctx != NULL);


  bump_reset(&ctx->ir_arena);
}

/*
 * =================================================================
 * --- 公共 API: 类型 (Types) ---
 * =================================================================
 */


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



  void *cached = ptr_hashmap_get(ctx->pointer_type_cache, (void *)pointee_type);
  if (cached)
  {
    return (IRType *)cached;
  }


  IRType *new_ptr_type = ir_type_create_ptr(ctx, pointee_type);
  if (!new_ptr_type)
  {

    return NULL;
  }




  ptr_hashmap_put(ctx->pointer_type_cache, (void *)pointee_type, (void *)new_ptr_type);

  return new_ptr_type;
}

/**
 * @brief 创建/获取一个数组类型 (唯一化)
 */
IRType *
ir_type_get_array(IRContext *ctx, IRType *element_type, size_t element_count)
{
  assert(ctx != NULL);
  assert(element_type != NULL);


  SizeHashMap *inner_map = (SizeHashMap *)ptr_hashmap_get(ctx->array_type_cache, (void *)element_type);

  if (!inner_map)
  {

    inner_map = sz_hashmap_create(&ctx->permanent_arena, 8);
    if (!inner_map)
      return NULL;


    ptr_hashmap_put(ctx->array_type_cache, (void *)element_type, (void *)inner_map);
  }


  IRType *array_type = (IRType *)sz_hashmap_get(inner_map, element_count);

  if (array_type)
  {
    return array_type;
  }


  array_type = ir_type_create_array(ctx, element_type, element_count);
  if (!array_type)
    return NULL;


  sz_hashmap_put(inner_map, element_count, (void *)array_type);

  return array_type;
}

/**
 * @brief 创建/获取一个 *匿名* 结构体 (按成员列表唯一化)
 */
IRType *
ir_type_get_anonymous_struct(IRContext *ctx, IRType **member_types, size_t member_count)
{
  assert(ctx != NULL);


  AnonStructKey temp_key = {.members = member_types, .count = member_count};



  IRType *struct_type = (IRType *)generic_hashmap_get(ctx->anon_struct_cache, &temp_key);

  if (struct_type)
  {
    return struct_type;
  }



  struct_type = ir_type_create_struct(ctx, member_types, member_count, NULL);
  if (!struct_type)
    return NULL;



  AnonStructKey *perm_key = BUMP_ALLOC(&ctx->permanent_arena, AnonStructKey);
  if (!perm_key)
    return NULL;


  perm_key->count = struct_type->as.aggregate.member_count;
  perm_key->members = struct_type->as.aggregate.member_types;




  generic_hashmap_put(ctx->anon_struct_cache, (void *)perm_key, (void *)struct_type);

  return struct_type;
}

/**
 * @brief 创建/获取一个 *命名* 结构体 (按名字唯一化)
 */
IRType *
ir_type_get_named_struct(IRContext *ctx, const char *name, IRType **member_types, size_t member_count)
{
  assert(ctx != NULL);
  assert(name != NULL && "Named struct must have a name");


  size_t name_len = strlen(name);
  IRType *struct_type = (IRType *)str_hashmap_get(ctx->named_struct_cache, name, name_len);

  if (struct_type)
  {

    if (struct_type->as.aggregate.member_count != member_count)
    {
      fprintf(stderr, "Struct '%s' re-definition with different member count!\n", name);
      assert(0);
    }
    for (size_t i = 0; i < member_count; i++)
    {
      if (struct_type->as.aggregate.member_types[i] != member_types[i])
      {
        fprintf(stderr, "Struct '%s' re-definition with different member types!\n", name);
        assert(0);
      }
    }
    return struct_type;
  }



  struct_type = ir_type_create_struct(ctx, member_types, member_count, name);
  if (!struct_type)
    return NULL;



  const char *interned_name = struct_type->as.aggregate.name;
  str_hashmap_put_preallocated_key(ctx->named_struct_cache, interned_name,
                                   strlen(interned_name),
                                   (void *)struct_type);

  return struct_type;
}

/**
 * @brief [!!] 新增: 创建/获取一个函数类型 (唯一化)
 * (复制 ir_type_get_anonymous_struct 的逻辑)
 */
IRType *
ir_type_get_function(IRContext *ctx, IRType *return_type, IRType **param_types, size_t param_count, bool is_variadic)
{
  assert(ctx != NULL);
  assert(return_type != NULL);



  FunctionTypeKey temp_key = {
    .return_type = return_type, .param_types = param_types, .param_count = param_count, .is_variadic = is_variadic};


  IRType *func_type = (IRType *)generic_hashmap_get(ctx->function_type_cache, &temp_key);

  if (func_type)
  {
    return func_type;
  }



  func_type = ir_type_create_function(ctx, return_type, param_types, param_count, is_variadic);
  if (!func_type)
    return NULL;



  FunctionTypeKey *perm_key = BUMP_ALLOC(&ctx->permanent_arena, FunctionTypeKey);
  if (!perm_key)
    return NULL;


  perm_key->return_type = func_type->as.function.return_type;
  perm_key->param_types = func_type->as.function.param_types;
  perm_key->param_count = func_type->as.function.param_count;
  perm_key->is_variadic = func_type->as.function.is_variadic;




  generic_hashmap_put(ctx->function_type_cache, (void *)perm_key, (void *)func_type);

  return func_type;
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


  void *cached = ptr_hashmap_get(ctx->undef_constant_cache, (void *)type);
  if (cached)
  {
    return (IRValueNode *)cached;
  }


  IRValueNode *new_undef = ir_constant_create_undef(ctx, type);
  if (!new_undef)
    return NULL;


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


DEFINE_GET_INT_CONSTANT(i8, int8_t, i8_hashmap, i8_constant_cache, i8_hashmap_get)
DEFINE_GET_INT_CONSTANT(i16, int16_t, i16_hashmap, i16_constant_cache, i16_hashmap_get)
DEFINE_GET_INT_CONSTANT(i32, int32_t, i32_hashmap, i32_constant_cache, i32_hashmap_get)
DEFINE_GET_INT_CONSTANT(i64, int64_t, i64_hashmap, i64_constant_cache, i64_hashmap_get)


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


  void *cached = str_hashmap_get(ctx->string_intern_cache, str, len);
  if (cached)
  {

    return (const char *)cached;
  }



  char *new_str = (char *)bump_alloc(&ctx->permanent_arena, len + 1, 1);
  if (!new_str)
    return NULL;

  memcpy(new_str, str, len);
  new_str[len] = '\0';





  bool put_ok = str_hashmap_put_preallocated_key(ctx->string_intern_cache,
                                                 new_str,
                                                 len,
                                                 (void *)new_str
  );

  if (!put_ok)
  {


    return NULL;
  }


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

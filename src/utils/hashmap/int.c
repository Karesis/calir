#include "utils/hashmap/int.h"
#include "utils/bump.h"
#include <assert.h>
#include <limits.h>
#include <string.h>

// 1. 包含 xxhash.h 并内联实现
#define XXH_INLINE_ALL
#include "utils/xxhash.h"

// 2. 定义模板宏工具
#define _INT_PASTE3(a, b, c) a##b##c
#define INT_PASTE3(a, b, c) _INT_PASTE3(a, b, c)
#define INT_FUNC(suffix) INT_PASTE3(INT_PREFIX, _hashmap_, suffix)

/*
 * ========================================
 * --- 实例化 64-bit 类型 ---
 * ========================================
 */

/* --- int64_t --- */
#define INT_PREFIX i64
#define INT_K_TYPE int64_t
#define INT_V_TYPE void *
#define INT_API_TYPE I64HashMap
#define INT_STRUCT_TYPE I64HashMap
#define INT_BUCKET_TYPE I64HashMapBucket
#define INT_EMPTY_K (INT64_MAX)
#define INT_TOMBSTONE_K (INT64_MAX - 1)
#include "utils/hashmap/int_template.inc"

/* --- uint64_t --- */
#define INT_PREFIX u64
#define INT_K_TYPE uint64_t
#define INT_V_TYPE void *
#define INT_API_TYPE U64HashMap
#define INT_STRUCT_TYPE U64HashMap
#define INT_BUCKET_TYPE U64HashMapBucket
#define INT_EMPTY_K (uint64_t)0
#define INT_TOMBSTONE_K (uint64_t)-1
#include "utils/hashmap/int_template.inc"

/*
 * ========================================
 * --- 实例化 32-bit 类型 ---
 * ========================================
 */

/* --- int32_t --- */
#define INT_PREFIX i32
#define INT_K_TYPE int32_t
#define INT_V_TYPE void *
#define INT_API_TYPE I32HashMap
#define INT_STRUCT_TYPE I32HashMap
#define INT_BUCKET_TYPE I32HashMapBucket
#define INT_EMPTY_K (INT32_MAX)
#define INT_TOMBSTONE_K (INT32_MAX - 1)
#include "utils/hashmap/int_template.inc"

/* --- uint32_t --- */
#define INT_PREFIX u32
#define INT_K_TYPE uint32_t
#define INT_V_TYPE void *
#define INT_API_TYPE U32HashMap
#define INT_STRUCT_TYPE U32HashMap
#define INT_BUCKET_TYPE U32HashMapBucket
#define INT_EMPTY_K (uint32_t)0
#define INT_TOMBSTONE_K (uint32_t)-1
#include "utils/hashmap/int_template.inc"

/*
 * ========================================
 * --- 实例化 16-bit 类型 ---
 * ========================================
 */

/* --- int16_t --- */
#define INT_PREFIX i16
#define INT_K_TYPE int16_t
#define INT_V_TYPE void *
#define INT_API_TYPE I16HashMap
#define INT_STRUCT_TYPE I16HashMap
#define INT_BUCKET_TYPE I16HashMapBucket
#define INT_EMPTY_K (INT16_MAX)
#define INT_TOMBSTONE_K (INT16_MAX - 1)
#include "utils/hashmap/int_template.inc"

/* --- uint16_t --- */
#define INT_PREFIX u16
#define INT_K_TYPE uint16_t
#define INT_V_TYPE void *
#define INT_API_TYPE U16HashMap
#define INT_STRUCT_TYPE U16HashMap
#define INT_BUCKET_TYPE U16HashMapBucket
#define INT_EMPTY_K (uint16_t)0
#define INT_TOMBSTONE_K (uint16_t)-1
#include "utils/hashmap/int_template.inc"

/*
 * ========================================
 * --- 实例化 8-bit 类型 ---
 * ========================================
 */

/* --- int8_t --- */
#define INT_PREFIX i8
#define INT_K_TYPE int8_t
#define INT_V_TYPE void *
#define INT_API_TYPE I8HashMap
#define INT_STRUCT_TYPE I8HashMap
#define INT_BUCKET_TYPE I8HashMapBucket
#define INT_EMPTY_K (INT8_MAX)
#define INT_TOMBSTONE_K (INT8_MAX - 1)
#include "utils/hashmap/int_template.inc"

/* --- uint8_t --- */
#define INT_PREFIX u8
#define INT_K_TYPE uint8_t
#define INT_V_TYPE void *
#define INT_API_TYPE U8HashMap
#define INT_STRUCT_TYPE U8HashMap
#define INT_BUCKET_TYPE U8HashMapBucket
#define INT_EMPTY_K (uint8_t)0
#define INT_TOMBSTONE_K (uint8_t)-1
#include "utils/hashmap/int_template.inc"

/*
 * ========================================
 * --- 实例化指针大小类型 ---
 * ========================================
 */

/* --- size_t --- */
#define INT_PREFIX sz
#define INT_K_TYPE size_t
#define INT_V_TYPE void *
#define INT_API_TYPE SizeHashMap
#define INT_STRUCT_TYPE SizeHashMap
#define INT_BUCKET_TYPE SizeHashMapBucket
#define INT_EMPTY_K (size_t)0
#define INT_TOMBSTONE_K (size_t)-1
#include "utils/hashmap/int_template.inc"

/* --- intptr_t --- */
#define INT_PREFIX iptr
#define INT_K_TYPE intptr_t
#define INT_V_TYPE void *
#define INT_API_TYPE IPtrHashMap
#define INT_STRUCT_TYPE IPtrHashMap
#define INT_BUCKET_TYPE IPtrHashMapBucket
#define INT_EMPTY_K (intptr_t)0
#define INT_TOMBSTONE_K (intptr_t)-1
#include "utils/hashmap/int_template.inc"

/* --- uintptr_t --- */
#define INT_PREFIX uptr
#define INT_K_TYPE uintptr_t
#define INT_V_TYPE void *
#define INT_API_TYPE UPtrHashMap
#define INT_STRUCT_TYPE UPtrHashMap
#define INT_BUCKET_TYPE UPtrHashMapBucket
#define INT_EMPTY_K (uintptr_t)0
#define INT_TOMBSTONE_K (uintptr_t)-1
#include "utils/hashmap/int_template.inc"

/*
 * ========================================
 * --- 清理宏 ---
 * ========================================
 */
#undef _INT_PASTE3
#undef INT_PASTE3
#undef INT_FUNC
#include "utils/hashmap/float.h"
#include "utils/bump.h"
#include <assert.h>
#include <string.h>

// 1. 包含 xxhash.h 并内联实现
#define XXH_INLINE_ALL
#include "utils/xxhash.h"

// 2. 定义模板宏工具
#define _FLOAT_PASTE3(a, b, c) a##b##c
#define FLOAT_PASTE3(a, b, c) _FLOAT_PASTE3(a, b, c)
#define FLOAT_FUNC(suffix) FLOAT_PASTE3(FLOAT_PREFIX, _hashmap_, suffix)

/*
 * ========================================
 * --- 实例化 float (f32) 哈希表 ---
 * ========================================
 */
#define FLOAT_PREFIX f32
#define FLOAT_K_TYPE float
#define FLOAT_V_TYPE void *
#define FLOAT_API_TYPE F32HashMap
#define FLOAT_STRUCT_TYPE F32HashMap
#define FLOAT_BUCKET_TYPE F32HashMapBucket
#include "utils/hashmap/float_template.inc"

/*
 * ========================================
 * --- 实例化 double (f64) 哈希表 ---
 * ========================================
 */
#define FLOAT_PREFIX f64
#define FLOAT_K_TYPE double
#define FLOAT_V_TYPE void *
#define FLOAT_API_TYPE F64HashMap
#define FLOAT_STRUCT_TYPE F64HashMap
#define FLOAT_BUCKET_TYPE F64HashMapBucket
#include "utils/hashmap/float_template.inc"

/*
 * ========================================
 * --- 清理宏 ---
 * ========================================
 */
#undef _FLOAT_PASTE3
#undef FLOAT_PASTE3
#undef FLOAT_FUNC
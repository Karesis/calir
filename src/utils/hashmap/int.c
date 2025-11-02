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


#include "utils/hashmap/int.h"
#include "utils/bump.h"
#include <assert.h>
#include <limits.h>
#include <string.h>


#define XXH_INLINE_ALL
#include "utils/xxhash.h"


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
#include "utils/hashmap/int_template.inc"

/* --- uint64_t --- */
#define INT_PREFIX u64
#define INT_K_TYPE uint64_t
#define INT_V_TYPE void *
#define INT_API_TYPE U64HashMap
#define INT_STRUCT_TYPE U64HashMap
#define INT_BUCKET_TYPE U64HashMapBucket
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
#include "utils/hashmap/int_template.inc"

/* --- uint32_t --- */
#define INT_PREFIX u32
#define INT_K_TYPE uint32_t
#define INT_V_TYPE void *
#define INT_API_TYPE U32HashMap
#define INT_STRUCT_TYPE U32HashMap
#define INT_BUCKET_TYPE U32HashMapBucket
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
#include "utils/hashmap/int_template.inc"

/* --- uint16_t --- */
#define INT_PREFIX u16
#define INT_K_TYPE uint16_t
#define INT_V_TYPE void *
#define INT_API_TYPE U16HashMap
#define INT_STRUCT_TYPE U16HashMap
#define INT_BUCKET_TYPE U16HashMapBucket
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
#include "utils/hashmap/int_template.inc"

/* --- uint8_t --- */
#define INT_PREFIX u8
#define INT_K_TYPE uint8_t
#define INT_V_TYPE void *
#define INT_API_TYPE U8HashMap
#define INT_STRUCT_TYPE U8HashMap
#define INT_BUCKET_TYPE U8HashMapBucket
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
#include "utils/hashmap/int_template.inc"

/* --- intptr_t --- */
#define INT_PREFIX iptr
#define INT_K_TYPE intptr_t
#define INT_V_TYPE void *
#define INT_API_TYPE IPtrHashMap
#define INT_STRUCT_TYPE IPtrHashMap
#define INT_BUCKET_TYPE IPtrHashMapBucket
#include "utils/hashmap/int_template.inc"

/* --- uintptr_t --- */
#define INT_PREFIX uptr
#define INT_K_TYPE uintptr_t
#define INT_V_TYPE void *
#define INT_API_TYPE UPtrHashMap
#define INT_STRUCT_TYPE UPtrHashMap
#define INT_BUCKET_TYPE UPtrHashMapBucket
#include "utils/hashmap/int_template.inc"

/*
 * ========================================
 * --- 清理宏 ---
 * ========================================
 */
#undef _INT_PASTE3
#undef INT_PASTE3
#undef INT_FUNC
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

#include "utils/hashmap/float.h"
#include "utils/bump.h"
#include <assert.h>
#include <string.h>

#define XXH_INLINE_ALL
#include "utils/xxhash.h"

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
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


#include "utils/temp_vec.h"
#include <string.h>


#define TEMP_VEC_INITIAL_CAPACITY 8

/**
 * @brief [内部] 确保向量至少有空间再容纳一个元素。
 * @return true 成功, false OOM
 */
static bool
temp_vec_grow_if_needed(TempVec *vec)
{
  if (vec->len < vec->capacity)
  {

    return true;
  }


  size_t new_cap = (vec->capacity == 0) ? TEMP_VEC_INITIAL_CAPACITY : vec->capacity * 2;


  void **new_data = BUMP_REALLOC_SLICE(vec->arena,
                                       void *,
                                       vec->data,
                                       vec->len,
                                       new_cap
  );

  if (new_data == NULL)
  {

    return false;
  }

  vec->data = new_data;
  vec->capacity = new_cap;
  return true;
}



void
temp_vec_init(TempVec *vec, Bump *arena)
{
  vec->arena = arena;
  vec->len = 0;
  vec->capacity = 0;
  vec->data = NULL;
}

void
temp_vec_destroy(TempVec *vec)
{

  (void)vec;
}

bool
temp_vec_push(TempVec *vec, void *element)
{

  if (!temp_vec_grow_if_needed(vec))
  {
    return false;
  }


  vec->data[vec->len++] = element;
  return true;
}
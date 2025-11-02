#include "utils/temp_vec.h"
#include <string.h> // for memcpy (虽然 BUMP_REALLOC_SLICE 已处理)

// 向量第一次分配时的默认容量 (元素个数)
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
    // 空间足够
    return true;
  }

  // --- 空间不足，需要增长 ---
  size_t new_cap = (vec->capacity == 0) ? TEMP_VEC_INITIAL_CAPACITY : vec->capacity * 2;

  // [!!] 关键：使用你提供的 bump.h 宏
  void **new_data = BUMP_REALLOC_SLICE(vec->arena, // bump_ptr
                                       void *,     // T
                                       vec->data,  // old_ptr
                                       vec->len,   // old_count (要复制的元素个数)
                                       new_cap     // new_count (要分配的新总容量)
  );

  if (new_data == NULL)
  {
    // OOM (内存溢出)
    return false;
  }

  vec->data = new_data;
  vec->capacity = new_cap;
  return true;
}

// --- 公共 API 实现 ---

void
temp_vec_init(TempVec *vec, Bump *arena)
{
  vec->arena = arena;
  vec->len = 0;
  vec->capacity = 0;
  vec->data = NULL; // 懒分配
}

void
temp_vec_destroy(TempVec *vec)
{
  // 无操作 (No-op)。Bump arena 会处理所有内存的释放。
  (void)vec;
}

bool
temp_vec_push(TempVec *vec, void *element)
{
  // 1. 确保容量
  if (!temp_vec_grow_if_needed(vec))
  {
    return false; // OOM
  }

  // 2. 添加元素
  vec->data[vec->len++] = element;
  return true;
}
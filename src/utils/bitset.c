// src/utils/bitset.c
#include "utils/bitset.h"
#include <assert.h> // for assert()
#include <string.h> // for memset, memcpy, memcmp

// --- 内部辅助宏 ---

// 计算 num_bits 需要多少个 64 位字
#define BITSET_NUM_WORDS(num_bits) (((num_bits) + 63) / 64)

// 计算 'bit' 在哪个 64 位字中
#define BITSET_WORD_INDEX(bit) ((bit) / 64)

// 计算 'bit' 在其 64 位字中的索引 (0-63)
#define BITSET_BIT_INDEX(bit) ((bit) % 64)

// 获取 'bit' 对应的 64 位掩码 (例如 0...010...0)
#define BITSET_WORD_MASK(bit) ((uint64_t)1 << BITSET_BIT_INDEX(bit))

// --- API 实现 ---

Bitset *
bitset_create(size_t num_bits, Bump *arena)
{
  // 1. 分配 Bitset 结构体本身，并零初始化
  // (num_bits, num_words 会被置 0, words 会被置 NULL)
  Bitset *bs = BUMP_ALLOC_ZEROED(arena, Bitset);

  bs->num_bits = num_bits;
  if (num_bits == 0)
  {
    bs->num_words = 0;
    bs->words = NULL;
    return bs;
  }

  bs->num_words = BITSET_NUM_WORDS(num_bits);

  // 2. 分配 64 位字数组，并零初始化 (这就是 bitset_clear_all)
  bs->words = BUMP_ALLOC_SLICE_ZEROED(arena, uint64_t, bs->num_words);

  return bs;
}

Bitset *
bitset_create_all(size_t num_bits, Bump *arena)
{
  // 1. 分配 Bitset 结构体
  Bitset *bs = BUMP_ALLOC(arena, Bitset);

  bs->num_bits = num_bits;
  if (num_bits == 0)
  {
    bs->num_words = 0;
    bs->words = NULL;
    return bs;
  }

  bs->num_words = BITSET_NUM_WORDS(num_bits);

  // 2. 分配 64 位字数组 (不进行零初始化)
  bs->words = BUMP_ALLOC_SLICE(arena, uint64_t, bs->num_words);

  // 3. 手动设置为全 1
  bitset_set_all(bs);

  return bs;
}

void
bitset_set(Bitset *bs, size_t bit)
{
  assert(bit < bs->num_bits && "Bitset index out of bounds");
  bs->words[BITSET_WORD_INDEX(bit)] |= BITSET_WORD_MASK(bit);
}

void
bitset_clear(Bitset *bs, size_t bit)
{
  assert(bit < bs->num_bits && "Bitset index out of bounds");
  bs->words[BITSET_WORD_INDEX(bit)] &= ~BITSET_WORD_MASK(bit);
}

bool
bitset_test(const Bitset *bs, size_t bit)
{
  assert(bit < bs->num_bits && "Bitset index out of bounds");
  return (bs->words[BITSET_WORD_INDEX(bit)] & BITSET_WORD_MASK(bit)) != 0;
}

void
bitset_set_all(Bitset *bs)
{
  if (bs->num_words == 0)
    return;

  size_t full_words = bs->num_words;
  size_t remaining_bits = bs->num_bits % 64;

  if (remaining_bits != 0)
  {
    // 如果 num_words > 0，至少有一个（可能是部分的）词
    if (bs->num_words > 0)
    {
      full_words = bs->num_words - 1;
    }

    // 设置最后一个部分词
    // (uint64_t)1 << 64 是未定义行为，但 remaining_bits 永远不会是 64
    bs->words[bs->num_words - 1] = ((uint64_t)1 << remaining_bits) - 1;
  }

  // 设置所有完整的词 (如果 remaining_bits == 0，这会设置所有词)
  for (size_t i = 0; i < full_words; i++)
  {
    bs->words[i] = (uint64_t)-1; // 0xFFFFFFFFFFFFFFFF
  }
}

void
bitset_clear_all(Bitset *bs)
{
  if (bs->num_words == 0)
    return;
  memset(bs->words, 0, bs->num_words * sizeof(uint64_t));
}

bool
bitset_equals(const Bitset *bs1, const Bitset *bs2)
{
  if (bs1->num_bits != bs2->num_bits)
  {
    return false;
  }
  if (bs1->num_words == 0)
  {
    return true; // 两个都是空集
  }
  return memcmp(bs1->words, bs2->words, bs1->num_words * sizeof(uint64_t)) == 0;
}

void
bitset_copy(Bitset *dest, const Bitset *src)
{
  assert(dest->num_bits == src->num_bits && "Bitset copy size mismatch");
  if (src->num_words == 0)
    return;
  memcpy(dest->words, src->words, src->num_words * sizeof(uint64_t));
}

void
bitset_intersect(Bitset *dest, const Bitset *src1, const Bitset *src2)
{
  assert(dest->num_bits == src1->num_bits && src1->num_bits == src2->num_bits && "Bitset op size mismatch");
  for (size_t i = 0; i < dest->num_words; i++)
  {
    dest->words[i] = src1->words[i] & src2->words[i];
  }
}

void
bitset_union(Bitset *dest, const Bitset *src1, const Bitset *src2)
{
  assert(dest->num_bits == src1->num_bits && src1->num_bits == src2->num_bits && "Bitset op size mismatch");
  for (size_t i = 0; i < dest->num_words; i++)
  {
    dest->words[i] = src1->words[i] | src2->words[i];
  }
}

void
bitset_difference(Bitset *dest, const Bitset *src1, const Bitset *src2)
{
  assert(dest->num_bits == src1->num_bits && src1->num_bits == src2->num_bits && "Bitset op size mismatch");
  for (size_t i = 0; i < dest->num_words; i++)
  {
    dest->words[i] = src1->words[i] & (~src2->words[i]);
  }
}

int
bitset_count_slow(const Bitset *bs)
{
  // C 语言没有内置的 popcount，所以我们用一个慢速循环
  // (在 GCC/Clang 上, -O2 可能会将其优化为 popcnt 指令)
  int count = 0;
  for (size_t i = 0; i < bs->num_words; i++)
  {
    uint64_t word = bs->words[i];
    // Brian Kernighan 算法
    while (word > 0)
    {
      word &= (word - 1);
      count++;
    }
  }

  // 清理最后一个词中无效的位
  size_t remaining_bits = bs->num_bits % 64;
  if (remaining_bits != 0 && bs->num_words > 0)
  {
    uint64_t last_word = bs->words[bs->num_words - 1];
    // 找出 *无效* 的位 (e.g., num_bits=65, 检查 66-127 位)
    uint64_t invalid_mask = (uint64_t)-1 << remaining_bits;
    uint64_t invalid_bits_set = last_word & invalid_mask;

    // 减去这些被错误计数的位
    while (invalid_bits_set > 0)
    {
      invalid_bits_set &= (invalid_bits_set - 1);
      count--;
    }
  }

  return count;
}
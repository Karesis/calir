#ifndef IR_LIST_H
#define IR_LIST_H
// 侵入式双向链表
#include <stddef.h> // for offsetof

/**
 * @brief 侵入式双向链表节点(intrusive doubly linked list)
 *
 * `prev` 指向前一个节点，`next` 指向后一个节点。
 * 对于链表头（哨兵节点），`prev` 指向最后一个节点，`next` 指向第一个节点。
 * 对于空链表，`prev` 和 `next` 都指向链表头自己。
 */
typedef struct IDList
{
  struct IDList *prev;
  struct IDList *next;
} IDList;

// 核心函数

/**
 * @brief 初始化一个链表头 (或一个独立的节点)
 * @param list 要初始化的链表头
 */
static inline void
list_init(IDList *list)
{
  list->prev = list;
  list->next = list;
}

/**
 * @brief 在两个已知节点之间插入一个新节点
 * @param prev 前一个节点
 * @param next 后一个节点
 * @param node 要插入的新节点
 */
static inline void
__list_add(IDList *prev, IDList *next, IDList *node)
{
  next->prev = node;
  node->next = next;
  node->prev = prev;
  prev->next = node;
}

/**
 * @brief 在链表尾部添加一个新节点
 * @param head 链表头
 * @param node 要添加的节点
 */
static inline void
list_add_tail(IDList *head, IDList *node)
{
  __list_add(head->prev, head, node);
}

/**
 * @brief 在链表头部添加一个新节点
 * @param head 链表头
 * @param node 要添加的节点
 */
static inline void
list_add(IDList *head, IDList *node)
{
  __list_add(head, head->next, node);
}

/**
 * @brief 从链表中删除一个节点
 * @param node 要删除的节点
 */
static inline void
list_del(IDList *node)
{
  node->next->prev = node->prev;
  node->prev->next = node->next;
  // 将节点重置为"未链接"状态 (可选，但有助于调试)
  list_init(node);
}

/**
 * @brief 检查链表是否为空
 * @param head 链表头
 * @return 1 (true) 如果为空, 0 (false) 如果不为空
 */
static inline int
list_empty(const IDList *head)
{
  return head->next == head;
}

// container_of 和 迭代器

/**
 * @brief container_of 宏
 * 通过一个结构体成员的指针，获取该结构体“容器”的指针。
 * @param ptr 成员的指针
 * @param type 容器结构体的类型 (如 IRFunction)
 * @param member 成员在结构体中的名字 (如 list_node)
 */
#define container_of(ptr, type, member) ((type *)((char *)(ptr) - offsetof(type, member)))

/**
 * @brief 获取链表中特定条目的宏
 * 这是 container_of 的一个特化版本，专门用于 IRList。
 */
#define list_entry(ptr, type, member) container_of(ptr, type, member)

/**
 * @brief 遍历链表 (正向)
 * @param head 链表头
 * @param iter_node 用于迭代的 IDList* 临时变量 (如 struct IDList *node)
 */
#define list_for_each(head, iter_node)                                                                                 \
  for ((iter_node) = (head)->next; (iter_node) != (head); (iter_node) = (iter_node)->next)

/**
 * @brief 遍历链表 (安全版，允许在遍历时删除节点)
 * @param head 链表头
 * @param iter_node 用于迭代的 IDList* 临时变量
 * @param temp_node 另一个 IDList* 临时变量，用于暂存 next 节点
 */
#define list_for_each_safe(head, iter_node, temp_node)                                                                 \
  for ((iter_node) = (head)->next, (temp_node) = (iter_node)->next; (iter_node) != (head);                             \
       (iter_node) = (temp_node), (temp_node) = (iter_node)->next)

#endif
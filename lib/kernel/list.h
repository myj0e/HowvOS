#ifndef __LIB_KERNEL_LIST_H
#define __LIB_KERNEL_LIST_H
#include "global.h"
#include "stdint.h"
#define offset(struct_type, member) (int)(&((struct_type*)0)->member)

/**
 * @brief 通过元素指针获取包含该元素的结构体指针。
 *
 * 该宏用于从一个结构体成员（元素）的指针反推出整个结构体的指针。它通过计算成员在结构体中的偏移量，
 * 并将元素指针减去该偏移量，从而得到结构体的起始地址。
 *
 * @param struct_type 结构体类型名称。
 * @param struct_member_name 结构体成员的名称。
 * @param elem_ptr 指向结构体成员的指针。
 *
 * @return 返回指向包含该成员的结构体的指针。
 */
#define elem2entry(struct_type, struct_member_name, elem_ptr) (struct_type*)((int)elem_ptr - offset(struct_type, struct_member_name))

/********** 定义链表结点成员结构 **********
 * 节点中不需要数据元，只需要前后指针即可*/
struct list_elem{
  struct list_elem* prev;   //前驱节点
  struct list_elem* next;   //后继节点
};

/* 链表结构，用来实现队列 */
struct list{
  /* head是队首，固定不变，不是第1个元素，第1个元素是head.next */
  struct list_elem head;
  /* tail是队尾，同样固定不变 */
  struct list_elem tail;
};

/* 自定义函数function，用于在list_traversal中做回调函数 */
typedef bool (function)(struct list_elem*, int arg);

void list_init(struct list*);
void list_insert_before(struct list_elem* before, struct list_elem* elem);
void list_push(struct list* plist, struct list_elem* elem);
void list_iterate(struct list* plist);
void list_append(struct list* plist, struct list_elem* elem);
void list_remove(struct list_elem* pelem);
struct list_elem* list_pop(struct list* plist);
bool list_empty(struct list* plist);
uint32_t list_len(struct list* plist);
struct list_elem* list_traversal(struct list* plist, function func, int arg);
bool elem_find(struct list* plist, struct list_elem* obj_elem);
#endif
#ifndef __FS_INODE_H__
#define __FS_INODE_H__

#include "stdint.h"
#include "list.h"
#include "global.h"

/* inode结构 */
struct inode {
    uint32_t i_no;     // inode编号
    uint32_t i_size;   // inode大小
    uint32_t i_open_cnts;  // inode打开次数
    bool write_deny;   // 写文件不能并行，进程写文件前检查此标识
    uint32_t i_sectors[13];  // 0-11是直接块，12是一级间接块指针，13是二级间接块指针
    struct list_elem inode_tag; // 用于加入到inode队列中
};

#endif


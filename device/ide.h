#ifndef __DEVICE_IDE_H
#define __DEVICE_IDE_H
#include "stdint.h"
#include "list.h"
#include "bitmap.h"
#include "sync.h"

/* 分区结构 */
struct partition {
  uint32_t start_lba;           //起始扇区
  uint32_t sec_cnt;             //扇区数
  struct disk* my_disk;         //分区所属硬盘
  struct list_elem part_tag;    //用于队列的标记
  char name[8];                 //分区名称
  struct super_block* sb;       //本分区的超级块
  struct bitmap block_bitmap;   //块位图
  struct bitmap inode_bitmap;   //i节点位图
  struct list open_inodes;      //本分区打开的i结点队列
};

/* 硬盘结构 */
struct disk{
  char name[8];         //本硬盘的名称
  struct ide_channel* my_channel;   //此块硬盘归属于哪个ide通道
  uint8_t dev_no;       //本硬盘是主0,还是从1
  struct partition prim_parts[4];
  struct partition logic_parts[8];  //逻辑分区数量无限，但我们这里限制了上限8
};

/* ata通道结构 */
struct ide_channel{
  char name[8];                 //本ata通道的名称 
  uint16_t port_base;           //本通道的起始端口号
  uint8_t irq_no;               //本通道所用的中断号
  struct lock lock;             //通道锁
  bool expection_intr;          //表示等待硬盘的中断
  struct semaphore disk_done;   //用于阻塞、唤醒驱动程序
  struct disk devices[2];       //一个通道上的主从两个硬盘
};

/* 以下内容是 硬盘设备控制相关 暴露出来给fs初始化使用 */
extern uint8_t channel_cnt;    //通道数
extern struct ide_channel channels[2];  //两个通道
extern struct list partition_list;    //分区队列

void ide_init(void);
void ide_read(struct disk* hd, uint32_t lba, void* buf, uint32_t sec_cnt);
void ide_write(struct disk* hd, uint32_t lba, void* buf, uint32_t sec_cnt);
void intr_hd_write(uint8_t irq_no);
#endif
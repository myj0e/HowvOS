#include "fs.h"
#include "ide.h"
#include "stdint.h"
#include "global.h"
#include "super_block.h"
#include "inode.h"
#include "dir.h"
#include "string.h"
#include "memory.h"
#include "debug.h"
#include "stdio-kernel.h"

struct partition* cur_part;     //默认情况下操作的是哪个分区

/* 在分区链表中找到名为part_name的分区，并将其指针赋值给cur_part
 * 将指定的分区挂载为当前工作分区
 * 
 * @param pelem 分区列表元素指针
 * @param arg 分区名称的整数表示
 * @return bool 挂载成功返回true，否则返回false
 * 
 * 此函数通过比较参数arg转换为的字符串与分区名称，来确定需要挂载的分区
 * 它首先读取并验证分区的超级块，然后将块位图和inode位图加载到内存中
 * 最后，它初始化分区的打开inode列表，并将该分区设置为当前工作分区
 */
static bool mount_partition(struct list_elem* pelem, int arg){
  // 将arg转换为字符串形式的分区名称
  char* part_name = (char*)arg;
  // 通过pelem获取对应的分区结构体
  struct partition* part = elem2entry(struct partition, part_tag, pelem);
  // 比较分区名称，以确定是否是需要挂载的分区
  if(!strcmp(part_name, part->name)){
    // 设置当前分区
    cur_part = part;
    // 获取当前分区所属的硬盘
    struct disk* hd = cur_part->my_disk;

    /* sb_buf用来存储从硬盘上读入的超级块 */
    struct super_block* sb_buf = (struct super_block*)sys_malloc(SECTOR_SIZE);

    /* 在内存中创建分区cur_part的超级块 */
    cur_part->sb = (struct super_block*)sys_malloc(sizeof(struct super_block));
    if(cur_part->sb == NULL){
      PANIC("alloc memory failed");
    }

    /* 读入超级块 */
    memset(sb_buf, 0, SECTOR_SIZE);
    ide_read(hd, cur_part->start_lba + 1, sb_buf, 1);

    /* 把缓冲区中的超级快复制到当前分区的sb中 */
    memcpy(cur_part->sb, sb_buf, sizeof(struct super_block));

    /**************** 将硬盘上的块位图读入到内存 ***************/
    cur_part->block_bitmap.bits = (uint8_t*)sys_malloc(sb_buf->block_bitmap_sects * SECTOR_SIZE);
    if(cur_part->block_bitmap.bits == NULL){
      PANIC("alloc memory failed!");
    }
    cur_part->block_bitmap.btmp_bytes_len = sb_buf->block_bitmap_sects * SECTOR_SIZE;
    /* 从硬盘上面读入块位图到分区的block_bitmap.bits*/
    ide_read(hd, sb_buf->block_bitmap_lba, cur_part->block_bitmap.bits, sb_buf->block_bitmap_sects);
    /***********************************************************/

    /**************** 将硬盘上的inode位图读入到内存 ***************/
    cur_part->inode_bitmap.bits = (uint8_t*)sys_malloc(sb_buf->inode_bitmap_sects * SECTOR_SIZE);
    if(cur_part->inode_bitmap.bits == NULL){
      PANIC("alloc memory failed!");
    }
    cur_part->inode_bitmap.btmp_bytes_len = sb_buf->inode_bitmap_sects * SECTOR_SIZE;
    /* 从硬盘上面读入块位图到分区的inode_bitmap.bits*/
    ide_read(hd, sb_buf->inode_bitmap_lba, cur_part->inode_bitmap.bits, sb_buf->inode_bitmap_sects);
    /***********************************************************/

    // 初始化当前分区的打开inode列表
    list_init(&cur_part->open_inodes);
    // 打印挂载信息
    printk("mount %s done!\n", part->name);
    /* 此处返回true是为了迎合主调函数list_traversal的实现，与函数本身无关，只有返回true该函数才会停止遍历 */
    return true;
  }
  // 如果当前分区不是要挂载的分区，则返回false，继续遍历
  return false;
}


/* 格式化分区，也就是初始化分区的元信息，创建文件系统 */// 格式化分区函数，根据分区信息初始化超级块、块位图、inode位图和inode表
/**
 * @brief 格式化指定的分区，初始化其超级块、块位图、inode位图和inode表。
 *
 * 该函数根据传入的分区信息，计算并初始化启动扇区、超级块、inode位图、inode表和数据区的布局，
 * 并将这些元数据写入磁盘。具体步骤包括：
 * 1. 计算各部分所需的扇区数；
 * 2. 初始化超级块结构并写入磁盘；
 * 3. 初始化块位图并写入磁盘；
 * 4. 初始化inode位图并写入磁盘；
 * 5. 初始化inode表并写入磁盘；
 * 6. 初始化根目录并写入磁盘。
 *
 * @param part 指向分区结构体的指针，包含分区的相关信息（如起始LBA、扇区总数等）。
 */
static void partition_format(struct partition* part){
  // 初始化启动扇区、超级块、inode位图和inode表所占用的扇区数
  uint32_t boot_sector_sects = 1;       // 启动扇区数
  uint32_t super_block_sects = 1;       // 超级块扇区数
  uint32_t inode_bitmap_sects = DIV_ROUND_UP(MAX_FILES_PER_PART, BITS_PER_SECTOR); // inode位图所占用的扇区数
  uint32_t inode_table_sects = DIV_ROUND_UP((sizeof(struct inode) * MAX_FILES_PER_PART), SECTOR_SIZE); // inode表所占扇区
  uint32_t used_sects = boot_sector_sects + super_block_sects + inode_bitmap_sects + inode_table_sects; // 已使用的扇区总数
  uint32_t free_sects = part->sec_cnt - used_sects; // 剩余的扇区数

  /*************************** 简单处理块位图占据的扇区数  *******************************/
  uint32_t block_bitmap_sects;
  block_bitmap_sects = DIV_ROUND_UP(free_sects, BITS_PER_SECTOR); // 空闲扇区位图所占的扇区数
  uint32_t block_bitmap_bit_len = free_sects - block_bitmap_sects; // 剩余空闲块扇区数
  block_bitmap_sects = DIV_ROUND_UP(block_bitmap_bit_len, BITS_PER_SECTOR); // 空闲位图扇区数
  /***************************************************************************************/

  /* 超级块初始化 */
  struct super_block sb;
  sb.magic = 0x20001109; // 超级块魔术值
  sb.sec_cnt = part->sec_cnt; // 分区扇区数
  sb.inode_cnt = MAX_FILES_PER_PART; // 最大文件数
  sb.part_lba_base = part->start_lba; // 分区起始LBA
  sb.block_bitmap_lba = sb.part_lba_base + 2; // 块位图起始LBA
  sb.block_bitmap_sects = block_bitmap_sects; // 块位图扇区数
  
  sb.inode_bitmap_lba = sb.block_bitmap_lba + sb.block_bitmap_sects; // inode位图起始LBA
  sb.inode_bitmap_sects = inode_bitmap_sects; // inode位图扇区数
  
  sb.inode_table_lba = sb.inode_bitmap_lba + sb.inode_bitmap_sects; // inode表起始LBA
  sb.inode_table_sects = inode_table_sects; // inode表扇区数

  sb.data_start_lba = sb.inode_table_lba + sb.inode_table_sects; // 数据区起始LBA
  sb.root_inode_no = 0; // 根目录inode号
  sb.dir_entry_size = sizeof(struct dir_entry); // 目录项大小

  // 打印分区信息
  printk("%s info:\n", part->name);
  printk("  magic:0x%x\n  part_lba_base:0x%x\n  all_sectors:0x%x\n  inode_cnt:0x%x\n  inode_bitmap_lba:0x%x\n\
        inode_bitmap_sectors:0x%x\n  inode_table_lba:0x%x\n  inode_table_sectors:0x%x\n  data_start_lba:0x%x\n",\
        sb.magic, sb.part_lba_base, sb.sec_cnt, sb.inode_cnt, sb.inode_bitmap_lba, sb.inode_bitmap_sects, sb.inode_table_lba, sb.inode_table_sects,\
        sb.data_start_lba);
  
  struct disk* hd = part->my_disk; 
  /***************************************
   * 1.将超级块写入本分区的1扇区
   * *************************************/
  ide_write(hd, part->start_lba + 1, &sb, 1);
  printk("  super_block_lba:0x%x\n", part->start_lba + 1);

  /* 找出数据量最大的元信息，用其尺寸做存储缓冲区 */
  uint32_t buf_size = (sb.block_bitmap_sects >= sb.inode_bitmap_sects ? sb.block_bitmap_sects : sb.inode_bitmap_sects);
  buf_size = (buf_size >= sb.inode_table_sects ? buf_size : sb.inode_table_sects) * SECTOR_SIZE;

  uint8_t* buf = (uint8_t*)sys_malloc(buf_size); // 申请内存缓冲区

  /******************************************
   * 2.将块位图初始化并且写入sb.block_bitmap_lba
   * ****************************************/
  buf[0] |= 0x01; // 第0个块预留给根目录
  uint32_t block_bitmap_last_byte = block_bitmap_bit_len / 8;
  uint8_t block_bitmap_last_bit = block_bitmap_bit_len % 8;
  uint32_t last_size = SECTOR_SIZE - (block_bitmap_last_byte % SECTOR_SIZE); // 块位图最后一扇区中剩余未使用的部分

  memset(&buf[block_bitmap_last_byte], 0xff, last_size); // 将位图最后一字节到其所在扇区结束的部分全置为1

  uint8_t bit_idx = 0;
  while(bit_idx <= block_bitmap_last_bit){
    buf[block_bitmap_last_byte] &= ~(1 << bit_idx++); 
  }
  ide_write(hd, sb.block_bitmap_lba, buf, sb.block_bitmap_sects); // 写入空闲块位图

  /*****************************************
   * 3.将inode位图初始化并且写入sb.inode_bitmap_lba
   * ***************************************/
  memset(buf, 0, buf_size); // 清空缓冲区
  buf[0] |= 0x1; // 第0个inode分给根目录
  ide_write(hd, sb.inode_bitmap_lba, buf, sb.inode_bitmap_sects); // 写入inode位图

  /*****************************************
   * 4.将inode数组初始化并写入sb.inode_table_lba
   *****************************************/
  memset(buf, 0, buf_size); // 清空缓冲区
  struct inode* i = (struct inode*)buf;
  i->i_size = sb.dir_entry_size * 2; // 大小为两个目录项的大小
  i->i_no = 0; // inode编号
  i->i_sectors[0] = sb.data_start_lba; // 初始化inode的第一个扇区号
  ide_write(hd, sb.inode_table_lba, buf, sb.inode_table_sects); // 写入inode表

  /******************************************
   * 5.将根目录写入sb.data_start_lba
   * ****************************************/
  memset(buf, 0, buf_size); // 清空缓冲区
  struct dir_entry* p_de = (struct dir_entry*)buf;
  memcpy(p_de->filename, ".", 1); // 初始化当前目录“.”
  p_de->i_no = 0;
  p_de->f_type = FT_DIRECTORY;
  p_de++;

  memcpy(p_de->filename, "..", 1); // 初始化当前目录的父目录".." 
  p_de->i_no = 0; // 根目录的父目录还是其自己
  p_de->f_type = FT_DIRECTORY;

  ide_write(hd, sb.data_start_lba, buf, 1); // 写入根目录的目录项

  printk("  root_dir_lba:0x%x\n", sb.data_start_lba);
  printk("%s format done\n", part->name);
  sys_free(buf); // 释放内存缓冲区
}


/* 在磁盘上搜索文件系统，若没有则格式化分区创建文件系统 */
void filesys_init(){
  uint8_t channel_no = 0, dev_no, part_idx = 0;

  /* sb.buf 用来存储从硬盘上读入的超级块 */
  struct super_block* sb_buf = (struct super_block*)sys_malloc(SECTOR_SIZE);
  if(sb_buf == NULL){
    PANIC("alloc memory failed!");
  }
  printk("searching filesystem ...\n");
  while(channel_no < channel_cnt){
    dev_no = 0;
    while(dev_no < 2){
      if(dev_no == 0){  //跳过hd60M.img
        dev_no++;
        continue;
      }
      struct disk* hd = &channels[channel_no].devices[dev_no];
      struct partition* part = hd->prim_parts;
      while(part_idx < 12){   //共有4个主分区和8个逻辑分区
        if(part_idx == 4){    //当idx为4的时候处理逻辑分区
          part = hd->logic_parts;
        }
        /* channels数组是全局变量，默认值为0,disk属于嵌套结构，
        * partition是disk的嵌套结构，所以partition中成员也默认为0
        * 下面处理存在的分区 */
        if(part->sec_cnt != 0){   //如果分区存在
          memset(sb_buf, 0, SECTOR_SIZE);
          /* 读出分区的超级块，根据魔数判断是否存在文件系统 */
          ide_read(hd, part->start_lba + 1, sb_buf, 1);   //这里start_lba + 1 是超级块所在的扇区
          if(sb_buf->magic == 0x20001109){
            printk("%s has filesystem\n", part->name);
          }else{
              printk("formatting %s's partition %s.......\n", hd->name, part->name);
              partition_format(part);
          }
        }
        part_idx++;
        part++;   //下一分区
      }
      dev_no++;   //下一磁盘
    }
    channel_no++;   //下一通道
  }
  sys_free(sb_buf);
  /* 确定默认操作的分区 */
  char default_part[8] = "sdb1";
  /* 挂载分区 */
  list_traversal(&partition_list, mount_partition, (int)default_part);
  printk("filesystem init done\n");
}
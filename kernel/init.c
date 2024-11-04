#include "init.h"
#include "print.h"
#include "interrupt.h"
#include "../device/timer.h"
#include "memory.h"
#include "thread.h"
#include "console.h"
#include "keyboard.h"
#include "tss.h"
#include "syscall-init.h"
#include "init.h"

/* 负责初始化所有模块 */
void init_all(){
  put_str("init_all\n");
  idt_init();       //初始化中断
  mem_init();       //初始化内存
  thread_init();    //初始化多线程
  timer_init();     //初始化PIT，依赖thread排在thread_init后
  console_init();   //初始化终端
  keyboard_init();  //初始化键盘
  tss_init();       //初始化TSS 
  syscall_init();
  ide_init();
}

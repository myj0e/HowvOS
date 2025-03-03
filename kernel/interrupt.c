#include "interrupt.h"
#include "stdint.h"
#include "global.h"
#include "io.h"
#include "print.h"

#define IDT_DESC_CNT 0x81           //目前总支持的中断数
#define PIC_M_CTRL 0x20             //主片控制端口
#define PIC_M_DATA 0x21             //主片数据端口
#define PIC_S_CTRL 0xA0             //从片控制端口
#define PIC_S_DATA 0xA1             //从片数据端口
#define EFLAGS_IF 0x00000200        //eflags寄存器中的if位为1
#define GET_EFLAGS(EFLAG_VAR) asm volatile("pushfl; pop %0" : "=g" (EFLAG_VAR))   //pushfl是指将eflags寄存器值压入栈顶

/*中断门描述符结构体*/
struct gate_desc {
  uint16_t func_offset_low_word;
  uint16_t selector;
  uint8_t dcount;                   //不用考虑，这里为固定值
  uint8_t attribute;
  uint16_t func_offset_high_word;
};

//静态函数声明
static void make_idt_desc(struct gate_desc* p_gdesc, uint8_t attr, intr_handler function);
static struct gate_desc idt[IDT_DESC_CNT];  //idt是中断描述符表，本质上是中断描述符数组

char* intr_name[IDT_DESC_CNT];              //用于保存异常的名字，这里感觉很像ELF文件的结构了



intr_handler idt_table[IDT_DESC_CNT];       //定义中断处理程序地址数组

extern intr_handler intr_entry_table[IDT_DESC_CNT];     //声明引用在kernel.S中的中断处理函数入口数组
extern uint32_t syscall_handler(void);      //单独的系统调用中断处理函数例程

/* 初始化可编程中断控制器 */
static void pic_init(void){
  /* 初始化主片 */
  outb(PIC_M_CTRL, 0x11);           //ICW1:边沿触发，级联8259,需要ICW4
  outb(PIC_M_DATA, 0x20);           //ICW2:起始中断向量号为0x20,也就是IRQ0的中断向量号为0x20
  outb(PIC_M_DATA, 0x04);           //ICW3:设置IR2接从片
  outb(PIC_M_DATA, 0x01);           //ICW4:8086模式，正常EOI，非缓冲模式，手动结束中断

  /* 初始化从片 */
  outb(PIC_S_CTRL, 0x11);           //ICW1:边沿触发，级联8259,需要ICW4
  outb(PIC_S_DATA, 0x28);           //ICW2：起始中断向量号为0x28
  outb(PIC_S_DATA, 0x02);           //ICW3:设置从片连接到主片的IR2引脚
  outb(PIC_S_DATA, 0x01);           //ICW4:同上

  /* 打开主片上的IR0,也就是目前只接受时钟产生的中断与键盘中断 */
  outb(PIC_M_DATA, 0xf8);           //OCW1:IRQ0时钟和IRQ1键盘 IRQ2级联从片外全部屏蔽,
  outb(PIC_S_DATA, 0xbf);           //OCW1:从片IRQ8~15，打开IRQ14硬盘中断，其余全部屏蔽

  put_str("pic init done!\n");
}

/*创建中断门描述符*/
static void make_idt_desc(struct gate_desc* p_gdesc, uint8_t attr, intr_handler function){  //参数分别为，描述符地址，属性，中断程序入口
  p_gdesc->func_offset_low_word = (uint32_t)function & 0x0000FFFF;
  p_gdesc->selector = SELECTOR_K_CODE;
  p_gdesc->dcount = 0;
  p_gdesc->attribute = attr;
  p_gdesc->func_offset_high_word = ((uint32_t)function & 0xFFFF0000) >> 16;
}

/*初始化中断描述符表*/
static void idt_desc_init(void){
  int i, lastindex = IDT_DESC_CNT-1;
  for(i = 0;i < IDT_DESC_CNT; i++){
    make_idt_desc(&idt[i],IDT_DESC_ATTR_DPL0, intr_entry_table[i]);
  }
  
  /* 单独处理系统调用，因为这里要使得用户能直接使用，所以系统调用对应的中断门dpl应为3,
   * 中断处理程序为单独的syscall_handler*/
  make_idt_desc(&idt[lastindex], IDT_DESC_ATTR_DPL3, syscall_handler);
  put_str("idt_desc_init_done!\n");
}

/* 通用的中断处理函数，一般用在出现异常的时候处理 */
static void general_intr_handler(uint8_t vec_nr){
  /* IRQ7和IRQ15会产生伪中断，IRQ15是从片上最后一个引脚，保留项，这俩都不需要处理 */
  if(vec_nr == 0x27 || vec_nr == 0x2f){
    return;
  } 
  /* 将光标置为0,从屏幕左上角清出一片打印异常信息的区域方便阅读 */
  set_cursor(0);    //这里是print.S中的设置光标函数，光标值范围是0～1999
  int cursor_pos = 0;
  while(cursor_pos < 320){
    put_char(' ');
    cursor_pos++;
  }
  set_cursor(0);    //重置光标值
  put_str("!!!!!!!!  exception message begin  !!!!!!!!\n");
  set_cursor(88);    //从第二行第8个字符开始打印
  put_str(intr_name[vec_nr]);
  if(vec_nr == 14){         //若为PageFault,将缺失的地址打印出来并悬停
    int page_fault_vaddr = 0;
    asm("movl %%cr2, %0" : "=r"(page_fault_vaddr));     //cr2存放造成PageFault的地址
    put_str("\npage fault addr is ");
    put_int(page_fault_vaddr);
  }
  put_str("\n!!!!!!!!   exception message end !!!!!!!");
  /* 能进入中断处理程序就表示已经在关中断情况下了，不会出现调度进程的情况，因此下面的死循环不会被中断 */
  while(1);
}

/* 完成一般中断处理函数注册以及异常名称注册 */
static void exception_init(void){
  int i;
  for(i  = 0; i < IDT_DESC_CNT; i++){
/* idt_table数组中的函数是在进入中断后根据中断向量号调用的
 * */
    idt_table[i] = general_intr_handler;    //这里初始化为最初的普遍处理函数
    intr_name[i] = "unknown";               //先统一赋值为unknown
  }
  intr_name[0] = "#DE Divide Error";
  intr_name[1] = "#DB Debug Exception";
  intr_name[2] = "NMI Interrupt";
  intr_name[3] = "#BP Breakpoint Exception";
  intr_name[4] = "#OF Overflow Exception";
  intr_name[5] = "#BR BOUND Range Exceeded Exception";
  intr_name[6] = "#UD Invalid Opcode Exception";
  intr_name[7] = "#NM Device Not Available Exception";
  intr_name[8] = "#DF Double Fault Exception";
  intr_name[9] = "Coprocessor Segment Overrun";
  intr_name[10] = "#TS Invalid TSS Exception";
  intr_name[11] = "#NP Segment Not Present";
  intr_name[12] = "#SS Stack Fault Exception";
  intr_name[13] = "#GP General Protection Exception";
  intr_name[14] = "#PF Page-Fault Exception";
  //intr_name[15]是保留项，未使用
  intr_name[16] = "#MF x87 FPU Floating-Point Error";
  intr_name[17] = "#AC Alignment Check Exception";
  intr_name[18] = "#MC Machine-Check Exception";
  intr_name[19] = "#XF SIMD Floating-Point Exception";
}

/* 开中断并返回开中断前的状态 */
enum intr_status intr_enable(){
  enum intr_status old_status;
  if (INTR_ON == intr_get_status()){
    old_status = INTR_ON;
    return old_status;
  }else{
    old_status = INTR_OFF;
    asm volatile("sti");     //开中断，sti指令将IF位置为1
    return old_status;
  }
}

/* 关中断并返回关中断前的状态 */
enum intr_status intr_disable(){
  enum intr_status old_status;
  if(INTR_ON == intr_get_status()){
    old_status = INTR_ON;
    asm volatile("cli" : : : "memory");
    return old_status;
  }else{
    old_status = INTR_OFF;
    return old_status;
  }
}

/* 将中断状态设置为status */
enum intr_status intr_set_status(enum intr_status status){
  return status & INTR_ON ? intr_enable() : intr_disable();
}

/* 获取当前中断状态 */
enum intr_status intr_get_status(){
  uint32_t eflags = 0;
  GET_EFLAGS(eflags);
  return (EFLAGS_IF & eflags) ? INTR_ON : INTR_OFF ; 
}

/* 在中断处理程序数组第vector_no个元素中注册安装中断处理程序function */
void register_handler(uint8_t vector_no, intr_handler function){
  /* idt_table数组中的函数是在进入中断后根据中断向量号调用的 */
  idt_table[vector_no] = function;
}

/*完成有关中断的所有初始化工作*/
void idt_init(){
  put_str("idt_init start\n");
  idt_desc_init();          //初始化idt描述符
  exception_init();         //异常名初始化并注册通常的中断处理函数
  pic_init();               //初始化8259A

  /* 加载idt */
  uint64_t idt_operand = ((sizeof(idt)-1) | ((uint64_t)(uint32_t)idt << 16));   //这里(sizeof(idt)-1)是表示段界限，占16位，然后我们的idt地址左移16位表示高32位，表示idt首地址
  asm volatile("lidt %0" : : "m" (idt_operand)); 
  put_str("idt_init done\n");
}


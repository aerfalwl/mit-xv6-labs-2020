#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct spinlock tickslock;
uint ticks;

extern char trampoline[], uservec[], userret[];

// in kernelvec.S, calls kerneltrap().
void kernelvec();

extern int devintr();

void
trapinit(void)
{
  initlock(&tickslock, "time");
}

// set up to take exceptions and traps while in the kernel.
void
trapinithart(void)
{
  w_stvec((uint64)kernelvec);
}

//
// handle an interrupt, exception, or system call from user space.
// called from trampoline.S
// 有很多原因都可以让程序运行进入到usertrap函数中来，比如系统调用，
// 运算时除以0，使用了一个未被映射的虚拟地址，或者是设备中断
void
usertrap(void)
{
  int which_dev = 0;

  if((r_sstatus() & SSTATUS_SPP) != 0)
    panic("usertrap: not from user mode");

  // send interrupts and exceptions to kerneltrap(),
  // since we're now in the kernel.
  w_stvec((uint64)kernelvec);

  struct proc *p = myproc();
  
  // save user program counter.
  // 保存用户态的程序计数器的地址至内存，防止内核进程调度其它
  // 进程导致epc寄存器被更新为新的进程的epc值
  p->trapframe->epc = r_sepc();


  // scause寄存器的值为8，说明是系统调用导致的进入usertrap()函数
  if(r_scause() == 8){
    // system call

    // 检查进程是否被杀掉
    if(p->killed)
      exit(-1);

    // sepc points to the ecall instruction,
    // but we want to return to the next instruction.
    // sepc寄存器保存的是用户系统调用前的指令地址，也就是ecall指令的地址
    // 我们希望从内核态返回用户态时，执行的是ecall下一条指令的地址，所以这里进行
    // 加4操作
    p->trapframe->epc += 4;

    // an interrupt will change sstatus &c registers,
    // so don't enable until done with those registers.
    // 中断总是会被RISC-V的trap硬件关闭
    // 但是我们在处理系统调用的时候打开中断，可以使中断更快的服务
    intr_on();

    syscall();
  } else if((which_dev = devintr()) != 0){
    // ok
  } else {
    printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
    printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
    p->killed = 1;
  }

  // 再次检查进程是否被杀掉，如果已经被杀掉，则没必要恢复该进程调用前的contex
  if(p->killed)
    exit(-1);

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2)
    yield();

  // 返回用户态需要执行的操作
  usertrapret();
}

//
// return to user space
//
void
usertrapret(void)
{
  struct proc *p = myproc();

  // we're about to switch the destination of traps from
  // kerneltrap() to usertrap(), so turn off interrupts until
  // we're back in user space, where usertrap() is correct.
  // 关闭中断，因为现在stvec寄存器仍然保存的是指向内核空间trap代码的位置，但是
  // 我们马上要将其设置为指向用户空间的trap代码的位置，如果当前不关闭中断
  // 则发生中端时，就是执行用户空间的trap代码，会导致内核出错
  intr_off();

  // send syscalls, interrupts, and exceptions to trampoline.S
  // 设置stvec寄存器指向trampoline代码(可在trampoline.S文件中查看)
  // 该代码的最后一行是sret指令，会返回用户空间，并将程序计数器设置为sepc寄存器保存的值
  // 同时，sret指令也会使能中断
  w_stvec(TRAMPOLINE + (uservec - trampoline));
  // set up trapframe values that uservec will need when
  // the process next re-enters the kernel.
  p->trapframe->kernel_satp = r_satp();         // kernel page table
  p->trapframe->kernel_sp = p->kstack + PGSIZE; // process's kernel stack
  p->trapframe->kernel_trap = (uint64)usertrap; // 保存指向usertrap函数的指针，这样trampoline代码才能跳转到这个函数
  // 从tp寄存器中获取当前cpu的hartid，并保存在trapframe中
  // 因为用户代码可能会修改这个数字，我们从寄存器中获取原数更加保险
  p->trapframe->kernel_hartid = r_tp();         // hartid for cpuid()

  // set up the registers that trampoline.S's sret will use
  // to get to user space.
  
  // set S Previous Privilege mode to User.
  unsigned long x = r_sstatus();
  x &= ~SSTATUS_SPP; // clear SPP to 0 for user mode
  x |= SSTATUS_SPIE; // enable interrupts in user mode
  w_sstatus(x);

  // set S Exception Program Counter to the saved user pc.
  // 将sepc寄存器的值设置为我们之间保存的用户程序计数器的值
  // 这样在trampoline代码最后执行sret指令后，就可以将用户程序计数器设置为sepc的值，
  // 从而使得用户代码恢复至系统调用后的下一条指令
  w_sepc(p->trapframe->epc);

  // tell trampoline.S the user page table to switch to.
  // 切换至用户的page table
  uint64 satp = MAKE_SATP(p->pagetable);

  // jump to trampoline.S at the top of memory, which 
  // switches to the user page table, restores user registers,
  // and switches to user mode with sret.
  // 计算出要跳转的汇编地址的代码，这里我们希望跳转至userret函数时，因为其中包括了
  // 返回用户空间的代码
  uint64 fn = TRAMPOLINE + (userret - trampoline);
  // 下边的函数调用，将跳转至trampoline.S中，且两个参数分别保存在a0寄存器和a1寄存器
  ((void (*)(uint64,uint64))fn)(TRAPFRAME, satp);
}

// interrupts and exceptions from kernel code go here via kernelvec,
// on whatever the current kernel stack is.
void 
kerneltrap()
{
  int which_dev = 0;
  uint64 sepc = r_sepc();
  uint64 sstatus = r_sstatus();
  uint64 scause = r_scause();
  
  if((sstatus & SSTATUS_SPP) == 0)
    panic("kerneltrap: not from supervisor mode");
  if(intr_get() != 0)
    panic("kerneltrap: interrupts enabled");

  if((which_dev = devintr()) == 0){
    printf("scause %p\n", scause);
    printf("sepc=%p stval=%p\n", r_sepc(), r_stval());
    panic("kerneltrap");
  }

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2 && myproc() != 0 && myproc()->state == RUNNING)
    yield();

  // the yield() may have caused some traps to occur,
  // so restore trap registers for use by kernelvec.S's sepc instruction.
  w_sepc(sepc);
  w_sstatus(sstatus);
}

void
clockintr()
{
  acquire(&tickslock);
  ticks++;
  wakeup(&ticks);
  release(&tickslock);
}

// check if it's an external interrupt or software interrupt,
// and handle it.
// returns 2 if timer interrupt,
// 1 if other device,
// 0 if not recognized.
int
devintr()
{
  uint64 scause = r_scause();

  if((scause & 0x8000000000000000L) &&
     (scause & 0xff) == 9){
    // this is a supervisor external interrupt, via PLIC.

    // irq indicates which device interrupted.
    int irq = plic_claim();

    if(irq == UART0_IRQ){
      uartintr();
    } else if(irq == VIRTIO0_IRQ){
      virtio_disk_intr();
    } else if(irq){
      printf("unexpected interrupt irq=%d\n", irq);
    }

    // the PLIC allows each device to raise at most one
    // interrupt at a time; tell the PLIC the device is
    // now allowed to interrupt again.
    if(irq)
      plic_complete(irq);

    return 1;
  } else if(scause == 0x8000000000000001L){
    // software interrupt from a machine-mode timer interrupt,
    // forwarded by timervec in kernelvec.S.

    if(cpuid() == 0){
      clockintr();
    }
    
    // acknowledge the software interrupt by clearing
    // the SSIP bit in sip.
    w_sip(r_sip() & ~2);

    return 2;
  } else {
    return 0;
  }
}


// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct kmem {
  struct spinlock lock;
  struct run *freelist;
} ;

// 即每个CPU一把锁，每个CPU一个空余的物理内存链表
struct kmem cpukmems[NCPU];
// 每个CPU的锁分配一个名字
char name[NCPU][10];


void
initcpulocks()
{
  for (int i = 0; i < NCPU; i++) {
    // 每个CPU锁的名字为kmem + CPUID
    snprintf(name[i], 10, "kmem%d", i);
    struct kmem* curr = &cpukmems[i];
    initlock(&curr->lock, name[i]);
    curr->freelist = 0;
  }
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
// 释放特定CPU上的物理内存
void
kfreeforcpu(void *pa, int id)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  struct kmem* curr = &cpukmems[id];

  acquire(&curr->lock);
  r->next = curr->freelist;
  curr->freelist = r;
  release(&curr->lock);
}

/**
 * 每个cpu分配一个物理内存链表，初始时平均分配
 * 即假设物理内存一共有100页，CPU个数为4，则每个CPU
 * 初始的物理内存个数为25。若一共102页，则最后一个CPU分配
 * 25 + 2 = 27页
 */
void
kinitforcpu(void *pa_start, void *pa_end) {
  pa_start = (void*)PGROUNDUP((uint64)pa_start);
  pa_end = (void*)PGROUNDDOWN((uint64)pa_end);
  int cnt = (pa_end - pa_start) / PGSIZE; // 物理页总个数
  int length = cnt / NCPU; // 每个CPU物理页的个数

  void* p = pa_start;
  for (int i = 0; i < NCPU; i++) {
    for (int j = 0; j < length; j++) {
      kfreeforcpu(p, i);  // 为该CPU分配内存
      p += PGSIZE;
    }
  }

  // 将剩余的物理页分配为最后一个CPU
  while (p + PGSIZE <= pa_end) {
    kfreeforcpu(p, NCPU - 1);
    p += PGSIZE;
  }

}

void
kinit()
{
  initcpulocks();
  kinitforcpu(end, (void*)PHYSTOP);
}

void
kfree(void* pa)
{
  push_off();
  int id = cpuid();
  pop_off();
  kfreeforcpu(pa, id);
}

// 从剩余的CPU中偷一页内存
void*
stealing(int id)
{
  struct run *r;
  for (int i = 0; i < NCPU; i++) {
    if (i == id) { // 如果是当前CPU，则跳过，因为当前CPU已经无内存
      continue;
    }
    struct kmem* curr = &cpukmems[i];
    acquire(&curr->lock);
    r = curr->freelist;
    if (r)
      curr->freelist = r->next;
    release(&curr->lock);

    if (r) {
      memset((char *) r, 5, PGSIZE);
      return (void*)r;
    }
  }
  return 0;
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  push_off();
  int id = cpuid();
  pop_off();

  struct run *r;

  struct kmem* curr = &cpukmems[id];
  acquire(&curr->lock);
  r = curr->freelist;
  if(r)
    curr->freelist = r->next;
  release(&curr->lock);

  if(r) { // 如果找到物理页，则返回
    memset((char *) r, 5, PGSIZE); // fill with junk
    return (void*)r;
  } else { // 当前CPU无可用内存，则从别的CPU偷内存
    return stealing(id);
  }
}




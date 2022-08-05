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

long pa_ref_cnt[ARR_MAX];

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  for (int i = 0; i < ARR_MAX; i++) {
    pa_ref_cnt[i] = 1;
  }
  freerange(end, (void*)PHYSTOP);
//  printf("total physical page: %d\n", ARR_MAX);
//  printf("begin: %p\n", end);
//  for (int i = 0; i < ARR_MAX; i++) {
//    if (pa_ref_cnt[i] != 0) {
//      printf("%d: %d\n", i, pa_ref_cnt[i]);
//    }
//  }
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  acquire(&kmem.lock);
  pa_ref_cnt[ARR_INDEX((uint64)pa)]--;
  if (pa_ref_cnt[ARR_INDEX((uint64)pa)] > 0) {
    release(&kmem.lock);
    return;
  }
  release(&kmem.lock);
  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r) {
    kmem.freelist = r->next;
    pa_ref_cnt[ARR_INDEX((uint64)r)] = 1;
  }
  release(&kmem.lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}

void
decrease_cnt(uint64 pa)
{
  acquire(&kmem.lock);
  pa_ref_cnt[ARR_INDEX(pa)]--;
  release(&kmem.lock);
}

void
increase_cnt(uint64 pa)
{
  acquire(&kmem.lock);
  pa_ref_cnt[ARR_INDEX(pa)]++;
  release(&kmem.lock);
}

long
get_ref_cnt(uint64 pa)
{
  return pa_ref_cnt[ARR_INDEX(pa)];
}

void
debug(void)
{
    for (int i = 0; i < ARR_MAX; i++) {
    if (pa_ref_cnt[i] > 1) {
      printf("%d: %d\n", i, pa_ref_cnt[i]);
    }
  }
}

void
debugfreepa(void)
{
  uint64 cnt = 0;
  acquire(&kmem.lock);
  struct run *r = kmem.freelist;
  while (r != 0) {
    cnt++;
    r = r->next;
  }
  printf("freepage when using list length: %d\n", cnt);
  int cnt2 = 0;
  char *p;
  p = (char*)PGROUNDUP((uint64)end);
  for(; p + PGSIZE <= (char*)PHYSTOP; p += PGSIZE) {
    if (pa_ref_cnt[ARR_INDEX((uint64)p)] == 0) {
      cnt2++;
    }
  }
  printf("freepage when using array table %d\n", cnt2);
  release(&kmem.lock);
}

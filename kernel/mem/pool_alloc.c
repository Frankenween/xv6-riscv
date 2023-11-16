#include "pool_alloc.h"

#include "../printf.h"
#include "../riscv.h"
#include "../util/spinlock.h"
#include "../util/string.h"
#include "memlayout.h"

void freerange(void *pa_start, void *pa_end);

extern char end[];  // first address after kernel.
                    // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

void init_pool_allocator() {
  initlock(&kmem.lock, "kmem");
  freerange(end, (void *)PHYSTOP);
}

void freerange(void *pa_start, void *pa_end) {
  char *p;
  p = (char *)PGROUNDUP((uint64)pa_start);
  for (; p + PGSIZE <= (char *)pa_end; p += PGSIZE) free_pool_allocator(p);
}

void free_pool_allocator(void *pa) {
  struct run *r;

  if (((uint64)pa % PGSIZE) != 0 || (char *)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run *)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

void *kalloc_pool_allocator() {
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if (r) kmem.freelist = r->next;
  release(&kmem.lock);

  if (r) memset((char *)r, 5, PGSIZE);  // fill with junk
  return (void *)r;
}

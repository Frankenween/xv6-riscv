// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "kalloc.h"

#include "buddy_alloc.h"
#include "../mem/memlayout.h"
#include "../riscv.h"

extern char end[];  // first address after kernel

void kinit() {
  char *base = (char *)PGROUNDUP((uint64)end);
  init_buddy(base, (void *)PHYSTOP);
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void kfree(void *pa) { free_buddy(pa); }

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *kalloc(void) { return malloc_buddy(PGSIZE); }

void *malloc(uint64 n) { return malloc_buddy(n); }

uint64 sys_havemem() { return havemem_buddy(); }

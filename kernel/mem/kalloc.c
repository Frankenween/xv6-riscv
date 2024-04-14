// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "kalloc.h"

#include "../dev/device_tree.h"
#include "../mem/memlayout.h"
#include "buddy_alloc.h"
#include "kernel/printf.h"

#define max(a, b) (((a) < (b)) ? (b) : (a))

extern char end[];  // first address after kernel

void kinit() {
  uint64 base = (uint64)end;

  struct ftd_header ftdHeader;
  if (dt_get_header(&ftdHeader)) {
    printf("No valid device tree structure found: all memory from address %p "
        "will be used\n", base);
  } else {
    printf("Device tree reservations:\n");
    struct ftd_reserve_entry entry;
    for (uint64 i = 0; dt_get_reserve_entry(ftdHeader, &entry, i); i++) {
      printf("  Reserved region [%p; %p)\n",
             entry.address, entry.address + entry.size);
      base = max(base, entry.address + entry.size);
    }
    base = max(base, (uint64)dt_get_address() + ftdHeader.total_size);
  }

  base = PGROUNDUP(base);
  printf("Using memory from %p\n", base);
  init_buddy((char*)base, (void *)PHYSTOP);
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

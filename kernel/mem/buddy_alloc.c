// This is a buddy allocator with allocated array modification
// It is almost stolen from my OS homework with my minor modifications

#include "buddy_alloc.h"

#include "kernel/printf.h"
#include "kernel/util/bitset.h"
#include "kernel/util/free_mem_list.h"
#include "kernel/util/spinlock.h"
#include "kernel/util/string.h"

static int nsizes;  // the number of entries in bd_sizes array

#define LEAF_SIZE 16          // The smallest block size
#define MAXSIZE (nsizes - 1)  // Largest index in bd_sizes array
#define BLK_SIZE(k) ((1L << (k)) * LEAF_SIZE)  // Size of block at size k
#define NBLK(k) (1 << (MAXSIZE - k))  // Number of block at size k
#define ROUNDUP(n, sz) \
  (((((n)-1) / (sz)) + 1) * (sz))  // Round up to the next multiple of sz

struct level_info {
  struct free_mem_list free;

  // here we keep XOR of flags for buddies: we merge blocks only when both are
  // not allocated
  char *allocated;
  char *split;
};

static struct level_info *lvl_sizes;
static void *allocator_base;
static struct spinlock lock;
static uint64 free_mem;

int first_level_contains(uint64 n) {
  int lvl = 0;
  uint64 sz = LEAF_SIZE;
  while (sz < n) {
    lvl++;
    sz <<= 1;
  }
  return lvl;
}

uint64 ptr_block_index(int k, const char *p) {
  uint64 n = p - (char *)allocator_base;
  return n / BLK_SIZE(k);
}

void *block_to_address(int k, uint64 block_index) {
  return (char *)allocator_base + (block_index * BLK_SIZE(k));
}

void *malloc_buddy(uint64 n) {
  int fk = first_level_contains(n);
  int k = fk;

  acquire(&lock);

  // Find the smallest block, which can be allocated
  for (; k < nsizes; k++) {
    if (!fm_list_empty(&lvl_sizes[k].free)) break;
  }
  // If no free blocks
  if (k >= nsizes) {
    release(&lock);
    return 0;
  }
  __sync_sub_and_fetch(&free_mem, BLK_SIZE(fk));
  char *p = fm_list_pop(&lvl_sizes[k].free);
  bit_invert(lvl_sizes[k].allocated, ptr_block_index(k, p) >> 1);
  for (; k > fk; k--) {
    char *buddy = p + BLK_SIZE(k - 1);
    bit_set(lvl_sizes[k].split, ptr_block_index(k, p));  // cur block is split
    bit_invert(lvl_sizes[k - 1].allocated,
               ptr_block_index(k - 1, p) >> 1);   // left child is allocated
    fm_list_push(&lvl_sizes[k - 1].free, buddy);  // buddy is available
  }
  release(&lock);
  return p;
}

// Get the size of block which was given by malloc
int ptr_block_size(const char *p) {
  for (int k = 0; k < MAXSIZE; k++) {
    if (bit_isset(lvl_sizes[k + 1].split, ptr_block_index(k + 1, p))) {
      return k;
    }
  }
  return 0;
}

void free_buddy(void *p) {
  int k = ptr_block_size(p);

  acquire(&lock);
  __sync_add_and_fetch(&free_mem, BLK_SIZE(k));
  free_mem += BLK_SIZE(k);
  for (; k < MAXSIZE; k++) {
    uint64 block_index = ptr_block_index(k, p);
    uint64 buddy = ((block_index & 1) == 0) ? block_index + 1 : block_index - 1;
    // mark that this block is now not allocated
    bit_invert(lvl_sizes[k].allocated, block_index >> 1);
    if (bit_isset(lvl_sizes[k].allocated, buddy >> 1)) {
      break;  // buddy is allocated
    }
    void *q = block_to_address(k, buddy);
    fm_list_remove(q);
    if ((buddy & 1) == 0) {
      p = q;  // we go upper and need to move p at the beginning of a block
    }
    // this pair is not split anymore
    bit_clear(lvl_sizes[k + 1].split, ptr_block_index(k + 1, p));
  }
  fm_list_push(&lvl_sizes[k].free, p);

  release(&lock);
}

uint64 havemem_buddy() {
  return __sync_add_and_fetch(&free_mem, 0);
}

// First block with size k that doesn't contain p
uint64 next_block_index(int k, char *p) {
  uint64 i = ptr_block_index(k, p);
  if ((p - (char *)allocator_base) % BLK_SIZE(k) != 0) i++;
  return i;
}

int log2(uint64 n) {
  int k = 0;
  while (n > 1) {
    k++;
    n = n >> 1;
  }
  return k;
}

// Mark memory from [start, stop), starting at size 0, as allocated.
void bd_mark(void *start, void *stop) {
  if (((uint64)start % LEAF_SIZE != 0) || ((uint64)stop % LEAF_SIZE != 0))
    panic("bd_mark: unaligned range");

  for (int k = 0; k < nsizes; k++) {
    uint64 bi = ptr_block_index(k, start);
    uint64 bj = next_block_index(k, stop);
    for (; bi < bj; bi++) {
      if (k > 0) {
        // if a block is allocated at size k, mark it as split too.
        bit_set(lvl_sizes[k].split, bi);
      }
      bit_invert(lvl_sizes[k].allocated, bi >> 1);
    }
  }
}

// If a block is marked as allocated and the buddy is free, put the
// buddy on the free list at size k.
uint64 bd_initfree_pair(int k, uint64 bi, char mark_prefix) {
  uint64 buddy = (bi % 2 == 0) ? bi + 1 : bi - 1;
  uint64 free = 0;
  if (bit_isset(lvl_sizes[k].allocated, bi >> 1)) {
    // one of the pair is free
    free = BLK_SIZE(k);
    if ((buddy > bi) == mark_prefix) {
      fm_list_push(&lvl_sizes[k].free,
                   block_to_address(k, buddy));  // put buddy on free list
    } else {
      fm_list_push(&lvl_sizes[k].free,
                   block_to_address(k, bi));  // put bi on free list
    }
  }
  return free;
}

// Initialize the free lists for each size k. For each size k, there
// are only two pairs that may have a buddy that should be on free list:
// bd_left and bd_right. This function is called after marking allocator
// structures, so a node is valid if it is inside a segment
uint64 bd_initfree(void *bd_left, void *bd_right) {
  uint64 free = 0;

  for (int k = 0; k < MAXSIZE; k++) {  // skip max size
    uint64 left = next_block_index(k, bd_left);
    uint64 right = ptr_block_index(k, bd_right);
    free += bd_initfree_pair(k, left, 1);
    if (right <= left) continue;
    free += bd_initfree_pair(k, right, 0);
  }
  return free;
}

// Mark the range [allocator_base; p) as allocated
uint64 bd_mark_data_structures(char *p) {
  uint64 meta = p - (char *)allocator_base;
  printf("buddy: %d meta bytes for managing %d bytes of memory\n", meta,
         BLK_SIZE(MAXSIZE));
  bd_mark(allocator_base, p);
  return meta;
}

// Mark the range [end, HEAPSIZE) as allocated
uint64 bd_mark_unavailable(void *end) {
  uint64 unavailable = BLK_SIZE(MAXSIZE) - (end - allocator_base);
  if (unavailable > 0) unavailable = ROUNDUP(unavailable, LEAF_SIZE);
  printf("buddy: %d bytes unavailable\n", unavailable);

  void *bd_end = allocator_base + BLK_SIZE(MAXSIZE) - unavailable;
  bd_mark(bd_end, allocator_base + BLK_SIZE(MAXSIZE));
  return unavailable;
}

// Initialize the buddy allocator: it manages memory from [base, end).
void init_buddy(void *base, void *end) {
  char *p = (char *)ROUNDUP((uint64)base, LEAF_SIZE);
  uint64 sz;

  initlock(&lock, "buddy");
  allocator_base = (void *)p;

  // compute the number of sizes we need to manage [base, end)
  nsizes = log2(((char *)end - p) / LEAF_SIZE) + 1;
  if ((char *)end - p > BLK_SIZE(MAXSIZE)) {
    nsizes++;  // round up to the next power of 2
  }

  printf("buddy: memory sz is %d bytes; allocate an size array of length %d\n",
         (char *)end - p, nsizes);

  // allocate bd_sizes array
  lvl_sizes = (struct level_info *)p;
  p += sizeof(struct level_info) * nsizes;
  memset(lvl_sizes, 0, sizeof(struct level_info) * nsizes);

  // initialize free list and allocate the alloc array for each size k
  for (int k = 0; k < nsizes; k++) {
    fm_list_init(&lvl_sizes[k].free);
    sz = sizeof(char) * ROUNDUP(NBLK(k), 8) / 16;
    if (sz == 0) {
      sz = 1;  // for a root we need the only flag
    }
    lvl_sizes[k].allocated = p;
    memset(lvl_sizes[k].allocated, 0, sz);
    p += sz;
  }

  // allocate the split array for each size k, except for k = 0, since
  // we will not split blocks of size k = 0, the smallest size.
  for (int k = 1; k < nsizes; k++) {
    sz = sizeof(char) * (ROUNDUP(NBLK(k), 8)) / 8;
    lvl_sizes[k].split = p;
    memset(lvl_sizes[k].split, 0, sz);
    p += sz;
  }
  p = (char *)ROUNDUP((uint64)p, LEAF_SIZE);

  // done allocating; mark the memory range [base, p) as allocated, so
  // that buddy will not hand out that memory.
  uint64 meta = bd_mark_data_structures(p);

  // mark the unavailable memory range [end, HEAP_SIZE) as allocated,
  // so that buddy will not hand out that memory.
  uint64 unavailable = bd_mark_unavailable(end);
  void *bd_end = allocator_base + BLK_SIZE(MAXSIZE) - unavailable;

  // initialize free lists for each size k
  uint64 free = bd_initfree(p, bd_end);
  free_mem = free;

  // check if the amount that is free is what we expect
  if (free != BLK_SIZE(MAXSIZE) - meta - unavailable) {
    printf("expected %d free bytes but got %d\n",
           BLK_SIZE(MAXSIZE) - meta - unavailable, free);
    panic("init_buddy: wrong free mem amount");
  }
}

#include "kstack_provider.h"

#include "kernel/mem/memlayout.h"
#include "kernel/util/spinlock.h"
#include "kernel/util/vector.h"

struct vector pool;
struct spinlock lock;
int next_id = 1;

void init_kstack_provider() {
  v_init(&pool);
  initlock(&lock, "kstack pool lock");
}

uint64 get_kstack_va() {
  uint64 va;
  acquire(&lock);
  if (pool.size > 0) {
    va = v_pop_back(&pool);
  } else {
    va = KSTACK(next_id);
    next_id++;
  }
  release(&lock);
  return va;
}

void return_kstack_va(uint64 va) {
  acquire(&lock);
  v_push_back(&pool, va);  // it's OK if push failed
  release(&lock);
}

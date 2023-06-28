#include "free_proc_pool.h"

#include "kernel/mem/kalloc.h"
#include "kernel/printf.h"

struct {
  // There can't be more than NCPU processes being watched
  struct proc *freed[NCPU * 2];
  struct spinlock pool_lock;
  int in_pool;
} free_proc_pool;

void init_pool() { initlock(&free_proc_pool.pool_lock, "pool lock"); }

void free_pool(int need_lock) {
  if (need_lock) acquire(&free_proc_pool.pool_lock);
  if (free_proc_pool.in_pool == 0) {
    goto free_pool_end;
  }
  for (int i = 0; i < NCPU * 2; i++) {
    struct proc *p = free_proc_pool.freed[i];
    if (p != 0) {
      if (p->watching == 0) {
        kfree(p);
        free_proc_pool.freed[i] = 0;
        free_proc_pool.in_pool--;
      }
    }
  }
free_pool_end:
  if (need_lock) release(&free_proc_pool.pool_lock);
}

void push_pool(struct proc *p) {
  acquire(&free_proc_pool.pool_lock);
  free_pool(0);
  int i = 0;
  for (; i < NCPU * 2; i++) {
    if (free_proc_pool.freed[i] == 0) {
      free_proc_pool.freed[i] = p;
      free_proc_pool.in_pool++;
      break;
    }
  }
  if (i == NCPU * 2) panic("push to free pool failed");
  release(&free_proc_pool.pool_lock);
}

void print_pool() {
  printf("free pool\n");
  for (int i = 0; i < NCPU * 2; i++) {
    if (free_proc_pool.freed[i] != 0) {
      printf("pid %d name %s\n", free_proc_pool.freed[i]->pid,
             free_proc_pool.freed[i]->name);
    }
  }
  printf("\n");
}

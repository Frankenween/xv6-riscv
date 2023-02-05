#include "protected_pointer.h"

#include "kernel/mem/kalloc.h"

void pp_init(struct protected_ptr *pp, void *ptr) {
  pp->ptr = ptr;
  initlock(&pp->lock, "protected ptr");
  acquire(&pp->lock);
}

void *pp_acquire_and_get(struct protected_ptr *pp) {
  if (pp == 0) return 0; // so we can always acquire non-existing elements
  acquire(&pp->lock);
  return pp->ptr;
}

int pp_test_free_acquire_and_set(struct protected_ptr *pp, void *ptr) {
  acquire(&pp->lock);
  if (pp->ptr == 0) {
    pp->ptr = ptr;
    return 1;
  }
  release(&pp->lock);
  return 0;
}
void pp_acquire_and_set(struct protected_ptr *pp, void *ptr) {
  acquire(&pp->lock);
  pp->ptr = ptr;
}

void pp_release(struct protected_ptr *pp) {
  if (pp == 0) return;
  release(&pp->lock);
}

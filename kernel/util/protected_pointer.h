#pragma once

#include "spinlock.h"

struct protected_ptr {
  void *ptr;
  struct spinlock lock;
};

// After init lock is acquired
void pp_init(struct protected_ptr *pp, void *ptr);
void *pp_acquire_and_get(struct protected_ptr *pp);

// If ptr is free, new data is written and lock is kept
// Otherwise lock is released and 0 is returned
int pp_test_free_acquire_and_set(struct protected_ptr *pp, void *ptr);
void pp_acquire_and_set(struct protected_ptr *pp, void *ptr);

void pp_release(struct protected_ptr *pp);

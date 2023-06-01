#pragma once

#include "spinlock.h"

struct rw_lock {
  struct spinlock read_lock;
  struct spinlock write_lock;
  int readers;
};

void rw_initlock(struct rw_lock *lock);
void rw_acquire_read(struct rw_lock *lock);
void rw_acquire_write(struct rw_lock *lock);
void rw_release_read(struct rw_lock *lock);
void rw_release_write(struct rw_lock *lock);

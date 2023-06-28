#include "rw_lock.h"

void rw_initlock(struct rw_lock *lock) {
  initlock(&lock->read_lock, "read");
  initlock(&lock->write_lock, "write");
  lock->readers = 0;
}

void rw_acquire_read(struct rw_lock *lock) {
  acquire(&lock->read_lock);

  lock->readers++;
  if (lock->readers == 1) acquire(&lock->write_lock);

  release(&lock->read_lock);
}

void rw_acquire_write(struct rw_lock *lock) {
  acquire(&lock->write_lock);
}

void rw_release_read(struct rw_lock *lock) {
  acquire(&lock->read_lock);

  lock->readers--;
  if (lock->readers == 0) release(&lock->write_lock);

  release(&lock->read_lock);
}

void rw_release_write(struct rw_lock *lock) {
  release(&lock->write_lock);
}

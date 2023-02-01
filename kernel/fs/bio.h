#pragma once

#include "fs.h"
#include "kernel/types.h"
#include "kernel/util/sleeplock.h"

struct bio {
  int valid;  // has data been read from disk?
  int disk;   // does disk "own" buf?
  uint dev;
  uint blockno;
  struct sleeplock lock;
  uint refcnt;
  struct bio *prev;  // LRU cache list
  struct bio *next;
  uchar data[BSIZE];
};

void binit(void);
struct bio *bread(uint, uint);
void brelse(struct bio *);
void bwrite(struct bio *);
void bpin(struct bio *);
void bunpin(struct bio *);

#pragma once

#include "kernel/types.h"

// vector keeps uint64 or pointers
struct vector {
  int size;
  int capacity;

  uint64 *data;
};

void v_init(struct vector *v);
void v_grow(struct vector *v, int new_capacity);
uint64 v_get(struct vector *v, int i);
void v_set(struct vector *v, int i, uint64 val);
void v_push_back(struct vector *v, uint64 val);
void v_clear(struct vector *v);

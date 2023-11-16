#pragma once

#include "../types.h"

// vector keeps uint64 or pointers
// it can have an array of spinlocks if needed
struct vector {
  int size;
  int capacity;

  uint64 *data;
};

void v_init(struct vector *v);

// Returns 0 on success, -1 otherwise
int v_grow(struct vector *v, int new_capacity);
uint64 v_get(struct vector *v, int i);
void v_set(struct vector *v, int i, uint64 val);

// Returns 0 on success, -1 otherwise
int v_push_back(struct vector *v, uint64 val);
void v_clear(struct vector *v);

// If result is negative, there was a failure
int v_replace_first_zero(struct vector *v, uint64 val);
uint64 v_pop_back(struct vector *v);

void v_resize(struct vector *v, int new_size);

#include "vector.h"

#include "kernel/mem/kalloc.h"
#include "kernel/printf.h"
#include "string.h"

void v_init(struct vector *v) {
  v->size = 0;
  v->capacity = 0;
  v->data = 0;
}

void v_grow(struct vector *v, int new_capacity) {
  uint64 *new_data = malloc(sizeof(uint64) * new_capacity);
  if (new_data == 0) panic("grow failed: data");

  uint64 cpy_mem = v->size;
  if (cpy_mem > new_capacity) {
    cpy_mem = new_capacity;
  }

  memset(new_data, 0, new_capacity * sizeof(uint64));  // empty space is 0
  memmove(new_data, v->data, cpy_mem * sizeof(uint64));
  if (v->data != 0) {
    kfree(v->data);
  }
  v->data = new_data;
  v->capacity = new_capacity;
}

uint64 v_get(struct vector *v, int i) {
  if (v->size <= i) panic("vector out of bounds get");
  return v->data[i];
}

void v_set(struct vector *v, int i, uint64 val) {
  if (v->size <= i) panic("vector out of bounds set");
  v->data[i] = val;
}

void v_push_back(struct vector *v, uint64 val) {
  if (v->size == v->capacity) {
    v_grow(v, (v->capacity == 0) ? 4 : v->capacity * 2);  // Buddy leaf size
  }
  __sync_fetch_and_add(&v->size, 1);
  v_set(v, v->size - 1, val);
}

void v_clear(struct vector *v) {
  v->size = 0;
  v->capacity = 0;
  if (v->data != 0) {
    kfree(v->data);
  }
}

int first_zero(struct vector *v) {
  int i = 0;
  for (; i < v->size; i++) {
    if (v_get(v, i) == 0) return i;
  }
  return i;
}

int v_replace_first_zero(struct vector *v, uint64 val) {
  int pos = first_zero(v);
  if (pos == v->capacity) {
    v_grow(v, (v->capacity == 0) ? 4 : v->capacity * 2);
  }
  if (pos == v->size) {
    v_push_back(v, val);
  } else {
    v_set(v, pos, val);
  }
  return pos;
}

uint64 v_pop_back(struct vector *v) {
  if (v->size == 0) {
    panic("vector pop back");
  }
  v->size--;
  return v->data[v->size];
}

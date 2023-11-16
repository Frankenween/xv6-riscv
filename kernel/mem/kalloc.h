#pragma once

#include "../types.h"

void* kalloc(void);
void kfree(void*);
void kinit(void);
void* malloc(uint64 n);
uint64 sys_havemem();

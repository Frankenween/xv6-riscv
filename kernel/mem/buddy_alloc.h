#pragma once

#include "../types.h"

void init_buddy(void* base, void* end);
void free_buddy(void* p);
void* malloc_buddy(uint64 n);
uint64 havemem_buddy();

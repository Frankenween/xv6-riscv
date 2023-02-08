#pragma once

#include "kernel/types.h"

void init_kstack_provider();
uint64 get_kstack_va();
void return_kstack_va(uint64 va);

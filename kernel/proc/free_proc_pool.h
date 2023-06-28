// A structure for taking care of dead processes, who were not freed yet

#pragma once
#include "proc.h"

void init_pool();

void free_pool(int need_lock);
void push_pool(struct proc *p);
void print_pool();

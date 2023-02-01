#pragma once

#include "kernel/types.h"

extern uint ticks;
void trapinit(void);
void trapinithart(void);
extern struct spinlock tickslock;
void usertrapret(void);

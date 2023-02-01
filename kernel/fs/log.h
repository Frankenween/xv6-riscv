#pragma once

#include "kernel/fs/bio.h"

void initlog(int, struct superblock*);
void log_write(struct bio*);
void begin_op(void);
void end_op(void);

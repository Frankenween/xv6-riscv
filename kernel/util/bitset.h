#pragma once

#include "kernel/types.h"

// Returns 1 if i-th bit in bitset is active
int bit_isset(const char *bitset, uint64 i);

// Sets i-th bit to 1
void bit_set(char *bitset, uint64 i);

// Sets i-th bit to 0
void bit_clear(char *bitset, uint64 i);

// Inverts i-th bit
void bit_invert(char *bitset, uint64 i);

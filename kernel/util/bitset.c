#include "bitset.h"

// Returns 1 if i-th bit in bitset is active
int bit_isset(const char *bitset, uint64 i) {
  return (bitset[i >> 3] & (1 << (i & 0b111))) == (1 << (i & 0b111));
}

// Sets i-th bit to 1
void bit_set(char *bitset, uint64 i) { bitset[i >> 3] |= (1 << (i & 0b111)); }

// Sets i-th bit to 0
void bit_clear(char *bitset, uint64 i) {
  bitset[i >> 3] &= ~(1 << (i & 0b111));
}

// Inverts i-th bit
void bit_invert(char *bitset, uint64 i) {
  bitset[i >> 3] ^= (1 << (i & 0b111));
}

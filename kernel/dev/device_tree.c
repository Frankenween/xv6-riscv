#include "device_tree.h"
#include "../printf.h"

volatile void* device_tree_ptr = 0;

uint32 change_endian32(uint32 x) {
  return ((x & 0xFF) << 24) |
           ((x & 0xFF00) << 8) |
           ((x & 0xFF0000) >> 8) |
           ((x & 0xFF000000) >> 24);
}

uint64 change_endian64(uint64 x) {
  return ((uint64)(change_endian32(x & 0xFFFFFFFF)) << 32) |
         change_endian32(x >> 32);
}

#define fix_endian32(obj) obj = change_endian32(obj)

struct ftd_header device_tree_get_header() {
  struct ftd_header header = *(struct ftd_header*)(device_tree_ptr);
  fix_endian32(header.magic);
  fix_endian32(header.total_size);
  fix_endian32(header.offset_dt_struct);
  fix_endian32(header.offset_dt_strings);
  fix_endian32(header.offset_mem_rsvmap);
  fix_endian32(header.version);
  fix_endian32(header.last_comp_version);
  fix_endian32(header.boot_cpuid_phys);
  fix_endian32(header.size_dt_strings);
  fix_endian32(header.size_dt_struct);

  if (header.magic != DEVICE_TREE_MAGIC) {
    printf("Invalid device tree magic: expected %x, got %x\n",
           DEVICE_TREE_MAGIC, header.magic);
    panic("device tree");
  }
  if (header.version != DEVICE_TREE_EXPECTED_VERSION) {
    printf("Unsupported device tree version: expected %d, got %d\n",
           DEVICE_TREE_EXPECTED_VERSION, header.version);
    panic("device tree");
  }
  printf("Device tree: got header\n");
  return header;
}

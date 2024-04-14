#include "device_tree.h"
#include "../printf.h"

void* volatile device_tree_ptr = 0;

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
#define fix_endian64(obj) obj = change_endian64(obj)

int dt_get_header(struct ftd_header *header) {
  *header = *(struct ftd_header*)(device_tree_ptr);
  fix_endian32(header->magic);
  fix_endian32(header->total_size);
  fix_endian32(header->offset_dt_struct);
  fix_endian32(header->offset_dt_strings);
  fix_endian32(header->offset_mem_rsvmap);
  fix_endian32(header->version);
  fix_endian32(header->last_comp_version);
  fix_endian32(header->boot_cpuid_phys);
  fix_endian32(header->size_dt_strings);
  fix_endian32(header->size_dt_struct);

  if (header->magic != DEVICE_TREE_MAGIC) {
    printf("Invalid device tree magic: expected %x, got %x\n",
           DEVICE_TREE_MAGIC, header->magic);
    return 1;
  }
  if (header->version != DEVICE_TREE_EXPECTED_VERSION) {
    printf("Unsupported device tree version: expected %d, got %d\n",
           DEVICE_TREE_EXPECTED_VERSION, header->version);
    return 2;
  }
  printf("Device tree: got header at %p\n", device_tree_ptr);
  return 0;
}

const void* dt_get_address() {
  return device_tree_ptr;
}

int dt_get_reserve_entry(struct ftd_header header, struct ftd_reserve_entry *entry, uint64 i) {
  *entry = *(struct ftd_reserve_entry*)(device_tree_ptr + header.offset_mem_rsvmap + i * sizeof(struct ftd_reserve_entry));
  fix_endian64(entry->address);
  fix_endian64(entry->size);
  if (entry->size == 0 && entry->address == 0) return 0;
  return 1;
}

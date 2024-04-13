#pragma once

#include "../types.h"

#define DEVICE_TREE_MAGIC 0xD00DFEED
#define DEVICE_TREE_EXPECTED_VERSION 17
#define DEVICE_TREE_LST_COMP_VERSION 16

struct ftd_header {
  // Should be 0xD00DFEED
  uint32 magic;
  // Size of device tree structure in bytes.
  // Paddings are included(the last one too)
  uint32 total_size;
  // Structure block offset from the beginning of the header in bytes
  uint32 offset_dt_struct;
  // Strings block offset from the beginning of the header in bytes
  uint32 offset_dt_strings;
  // Memory reservation block offset from the beginning of the header in bytes
  uint32 offset_mem_rsvmap;
  // Version of device tree structure.
  // Should be 17
  uint32 version;
  // Lowest version with which current version is backwards compatible.
  // Should be 16
  uint32 last_comp_version;
  // Physical ID of system's boot CPU
  uint32 boot_cpuid_phys;
  // Byte length of string block section
  uint32 size_dt_strings;
  // Byte length if structure block section
  uint32 size_dt_struct;
};

uint32 change_endian32(uint32 x);

uint64 change_endian64(uint64 x);

struct ftd_header device_tree_get_header();

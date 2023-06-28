#pragma once

// A list for keeping empty structures, which are allocated
// Links are kept instead of structures, so struct size must be >= 16 bytes
// We cannot keep valid structures in this list, only places where they can be
// placed. Any ptr cannot be kept in more than one list
struct free_mem_list {
  struct free_mem_list* next;
  struct free_mem_list* prev;
};

void fm_list_init(struct free_mem_list*);
void fm_list_remove(struct free_mem_list*);
void fm_list_push(struct free_mem_list*, void*);
void* fm_list_pop(struct free_mem_list*);
void fm_list_print(struct free_mem_list*);
int fm_list_empty(struct free_mem_list*);

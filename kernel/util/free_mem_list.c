#include "free_mem_list.h"

#include "kernel/printf.h"

void fm_list_init(struct free_mem_list *lst) {
  lst->next = lst;
  lst->prev = lst;
}

int fm_list_empty(struct free_mem_list *lst) { return lst->next == lst; }

void fm_list_remove(struct free_mem_list *e) {
  e->prev->next = e->next;
  e->next->prev = e->prev;
}

void *fm_list_pop(struct free_mem_list *lst) {
  if (lst->next == lst) panic("pop from empty list");
  struct free_mem_list *p = lst->next;
  fm_list_remove(p);
  return (void *)p;
}

void fm_list_push(struct free_mem_list *lst, void *p) {
  struct free_mem_list *e = (struct free_mem_list *)p;
  e->next = lst->next;
  e->prev = lst;
  lst->next->prev = p;
  lst->next = e;
}

void fm_list_print(struct free_mem_list *lst) {
  for (struct free_mem_list *p = lst->next; p != lst; p = p->next) {
    printf(" %p", p);
  }
  printf("\n");
}

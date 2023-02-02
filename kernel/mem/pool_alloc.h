#pragma once

void init_pool_allocator();
void free_pool_allocator(void* pa);
void* kalloc_pool_allocator();

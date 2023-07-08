#pragma once
#include <common/defines.h>
#include <aarch64/mmu.h>
#include <driver/memlayout.h>
#include <common/list.h>
#include <common/spinlock.h>
#include <kernel/printk.h>
#include <common/rc.h>
#define PAGE_START (PAGE_BASE((u64)&end) + PAGE_SIZE)
#define ALLOC_PAGE_MAX  (PHYSTOP / PAGE_SIZE)

#define REVERSED_PAGES 1024 //Reversed pages

extern char end[];
SpinLock mem_lock;

RefCount* zero_cnt;

typedef struct page{
	RefCount ref;
} page;

WARN_RESULT void* kalloc_page();
void kfree_page(void*);

// void* buddy_alloc(isize);

WARN_RESULT void* kalloc(isize);
void kfree(void*);

u64 left_page_cnt();
WARN_RESULT void* get_zero_page();
bool check_zero_page();
u32 write_page_to_disk(void* ka);
void read_page_from_disk(void* ka, u32 bno);

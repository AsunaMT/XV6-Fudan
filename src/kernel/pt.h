#pragma once

#include <aarch64/mmu.h>
#include <common/list.h>

#define PTE_NUM (PAGE_SIZE / sizeof(PTEntriesPtr))

typedef struct pgdir
{
    PTEntriesPtr pt;
    SpinLock lock; 
    ListNode section_head;
    bool online;
} pgdir;

void init_pgdir(struct pgdir* pgdir);
WARN_RESULT PTEntriesPtr get_pte(struct pgdir* pgdir, u64 va, bool alloc);
void vmmap(struct pgdir* pd, u64 va, void* ka, u64 flags);
void free_pgdir(struct pgdir* pgdir);
void attach_pgdir(struct pgdir* pgdir);
int copyout(struct pgdir* pd, void* va, void *p, usize len);
int copy_pgdir(struct pgdir* from_pd, struct pgdir* to_pd);

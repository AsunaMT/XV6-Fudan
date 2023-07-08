#include <kernel/pt.h>
#include <kernel/mem.h>
#include <common/string.h>
#include <aarch64/intrinsic.h>
#include "pt.h"
#include <kernel/paging.h>
#include <kernel/paging.h>

PTEntriesPtr get_pte(struct pgdir* pgdir, u64 va, bool alloc)
{
    // TODO
    // Return a pointer to the PTE (Page Table Entry) for virtual address 'va'
    // If the entry not exists (NEEDN'T BE VALID), allocate it if alloc=true, or return NULL if false.
    // THIS ROUTINUE GETS THE PTE, NOT THE PAGE DESCRIBED BY PTE.
    PTEntriesPtr pt0, this_pt1, this_pt2, this_pt3;
    //printk("1\n");
    if(pgdir->pt == NULL){
        pgdir->pt = kalloc_page();
        memset(pgdir->pt, 0, PAGE_SIZE);
    }
    //printk("2\n");
    pt0 = pgdir->pt;
    if(pt0[VA_PART0(va)] == NULL) {
        if(!alloc) return NULL;
        this_pt1 = kalloc_page();
        memset(this_pt1, 0, PAGE_SIZE);
        pt0[VA_PART0(va)] = K2P(this_pt1) | PTE_TABLE;
    } else {
        this_pt1 = (PTEntriesPtr)P2K(PTE_ADDRESS(pt0[VA_PART0(va)]));
    }
    //printk("3\n");
    if(this_pt1[VA_PART1(va)] == NULL) {
        if(!alloc) return NULL;
        this_pt2 = kalloc_page();
        memset(this_pt2, 0, PAGE_SIZE);
        this_pt1[VA_PART1(va)] = K2P(this_pt2) | PTE_TABLE;
    } else {
        this_pt2 = (PTEntriesPtr)P2K(PTE_ADDRESS(this_pt1[VA_PART1(va)]));
    }
        //printk("4\n");
    if(this_pt2[VA_PART2(va)] == NULL) {
        if(!alloc) return NULL;
        this_pt3 = kalloc_page();
        memset(this_pt3, 0, PAGE_SIZE);
        this_pt2[VA_PART2(va)] = K2P(this_pt3) | PTE_TABLE;
    } else {
        this_pt3 = (PTEntriesPtr)P2K(PTE_ADDRESS(this_pt2[VA_PART2(va)]));
    }
       // printk("5\n");
    return &this_pt3[VA_PART3(va)];
}

void vmmap(pgdir* pd, u64 va, void* ka, u64 flags) {
    auto x = get_pte(pd, va, true);
    *(u64*)x = K2P(ka) | flags;
    //*get_pte(pd, va, true) = K2P(ka) | flags;
       // printk("5\n");
}

void init_pgdir(struct pgdir* pgdir) {
    pgdir->pt = kalloc_page();
    memset(pgdir->pt, 0, PAGE_SIZE);
    init_spinlock(&pgdir->lock);
    pgdir->online = false;
    init_list_node(&pgdir->section_head);
    init_sections(&pgdir->section_head);
}

#define INVALID_PTE(addr) (!((u64)addr & PTE_VALID))    //NULL也成立
void free_pgdir(struct pgdir* pgdir)
{
    // TODO
    // Free pages used by the page table. If pgdir->pt=NULL, do nothing.
    // DONT FREE PAGES DESCRIBED BY THE PAGE TABLE
    //printk("5\n");
    free_sections(pgdir);
    //printk("5\n");

    PTEntriesPtr pt0 = pgdir->pt;
    if(pt0 == NULL) return;
    for(int i = 0; i < (int)PTE_NUM; i++){
        if(INVALID_PTE(pt0[i])) continue;
        PTEntriesPtr pt1 = (PTEntriesPtr)P2K(PTE_ADDRESS(pt0[i]));
        for(int j = 0; j < (int)PTE_NUM; j++){
            if(INVALID_PTE(pt1[j])) continue;
            PTEntriesPtr pt2 = (PTEntriesPtr)P2K(PTE_ADDRESS(pt1[j]));
            for(int k = 0; k < (int)PTE_NUM; k++){
                //printk("i:%d,j:%d,k:%d\n", i, j, k);
                if(INVALID_PTE(pt2[k])) continue;
                PTEntriesPtr pt3 = (PTEntriesPtr)P2K(PTE_ADDRESS(pt2[k]));
                kfree_page(pt3);
            }
            kfree_page(pt2);
        }
        kfree_page(pt1);
    }
    kfree_page(pt0);
    pgdir->pt = NULL;
    //free sections
    // auto node = pgdir->section_head->next;
    // while(node != &pgdir->section_head){
    //     for(u64 addr = last->begin; addr < last->end; addr += PAGE_SIZE){
    //         kfree_page((void*)addr);
    //     }
    //     auto temp = node->next;
    //     _detach_from_list(node);
    //     kfree(section_of(node));
    //     node = temp; 
    // }
}
struct proc* thisproc();
void attach_pgdir(struct pgdir* pgdir)
{
    auto this_pgdir = &thisproc()->pgdir;
    if(this_pgdir){
        _acquire_spinlock(&this_pgdir->lock);
        this_pgdir->online = false;
        _release_spinlock(&this_pgdir->lock);
    }
    _acquire_spinlock(&pgdir->lock);
    pgdir->online = true;
    _release_spinlock(&pgdir->lock);
    extern PTEntries invalid_pt;
    if (pgdir->pt)
        arch_set_ttbr0(K2P(pgdir->pt));
    else
        arch_set_ttbr0(K2P(&invalid_pt));
}

/*
 * Copy len bytes from p to user address va in page table pgdir.
 * Allocate physical pages if required.
 * Useful when pgdir is not the current page table.
 */
int copyout(struct pgdir* pd, void* va, void *p, usize len){
    // TODO
    ASSERT(pd != &thisproc()->pgdir);
    u64 buf = (u64)p;
    while(len > 0){
        u64 va0 = PAGE_BASE((u64)va);
        auto pte = get_pte(pd, va0, true);
        void* pa0;
        ASSERT(pte);
        if ((*pte & PTE_VALID)){
            pa0 = (void*)P2K(PTE_ADDRESS(*pte));
        } else {
            pa0 = (void*)kalloc_page();
            vmmap(pd, va0, (void*)pa0, PTE_RO | PTE_USER_DATA);
        }
        u64 n = PAGE_SIZE - ((u64)va - va0);
        if(n > len)
            n = len;
        memmove(pa0 + ((u64)va - va0), (void*)buf, n);
        len -= n;
        buf += n;
        va = (void*)(va0 + PAGE_SIZE);
    }
    return 0;
}

int copy_pgdir(struct pgdir* from_pd, struct pgdir* to_pd){
    //printk("in copy pgdir, thisproc id:%d\n", thisproc()->pid);
    // auto from_head = &(from_pgdir->section_head);
    // _for_in_list(node, from_head){
    //     if (node == from_head)	continue;
    //     struct section* sec = section_of(node);
	// 	printk("sec flags:%llx\n", sec->flags);
	// 	printk("sec begin:%llx, end:%llx\n", sec->begin, sec->end);
	// }
    // ASSERT(to_pd != from_pd);
    // copy_sections(&from_pd->section_head, &to_pd->section_head);
    // //printk("in copy pgdir, thisproc id:%d\n", thisproc()->pid);
    // _for_in_list(node, &to_pd->section_head){
    //     if (node == &to_pd->section_head)  continue;
    //     struct section* sec = section_of(node);
    //     if (sec->end == sec->begin){
    //         continue;
    //     }
    //     ASSERT(sec->end > sec->begin);
    //     for (u64 va_pg = sec->begin; va_pg < sec->end; va_pg += PAGE_SIZE){
    //         void* ka1 = alloc_page_for_user();
    //         if (ka1 == NULL)  return -1;
    //         u64* pte = get_pte(from_pd, va_pg, false);
    //         if(!pte) printk("pte null, va:%llx\n", va_pg);
    //         ASSERT(*pte);
    //         void* addr = (void*)P2K(PTE_ADDRESS(*pte));
    //         memmove(ka1, addr, PAGE_SIZE);
    //         vmmap(to_pd, va_pg, ka1, PTE_FLAGS(*pte));
    //     }
    // }
    // return 0;
    free_sections(to_pd);
    // while (!_empty_list(&to_pd->section_head)){
	// 	_detach_from_list(to_pd->section_head.next);
	// }
    _for_in_list(node, &from_pd->section_head) {
        if (node == &from_pd->section_head)  continue;
        struct section* from_sec = section_of(node);
        struct section* to_sec = kalloc(sizeof(struct section));
        init_sleeplock(&to_sec->sleeplock);
		to_sec->flags = from_sec->flags;
        to_sec->begin = from_sec->begin;
		to_sec->end = from_sec->end;
        _insert_into_list(&to_pd->section_head, &to_sec->stnode);

        for (u64 va = from_sec->begin; va < from_sec->end; va += PAGE_SIZE) {
            auto pte = get_pte(from_pd, va, false);
            u64 flags = PTE_FLAGS(*pte);
			u64 from_ka = P2K(PTE_ADDRESS(*pte));
            void* ka = alloc_page_for_user();
            memmove(ka, (void*)from_ka, PAGE_SIZE);
            vmmap(to_pd, va, ka, flags);
        }
    }
    return 0;
}
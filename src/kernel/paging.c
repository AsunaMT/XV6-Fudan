#include <kernel/proc.h>
#include <aarch64/mmu.h>
#include <fs/block_device.h>
#include <fs/cache.h> 
#include <kernel/paging.h>
#include <common/defines.h>
#include <kernel/pt.h>
#include <common/sem.h>
#include <common/list.h>
#include <kernel/mem.h>
#include <kernel/sched.h>
#include <common/string.h>
#include <kernel/printk.h>
#include <kernel/init.h>
#include "paging.h"

#define get_pages_size(size) ((size) << PAGE_BITS)
#define has_flag(val, flag) ((u64)(val) & (u64)(flag))
#define no_flag(val, flag) (!((u64)(val) & (u64)(flag)))
#define bno_of(pte) (*(pte) >> 12)
#define set_bno(pte, bno) (*(pte) = (bno) << 12 | ((*pte) & 0xFFFull))

define_rest_init(paging){
	//TODO init		
}

static struct section* get_heap_section(struct pgdir* pgdir){
	_for_in_list(node, &pgdir->section_head){
		if(node == &pgdir->section_head) continue;
		auto res = section_of(node);
		if(has_flag(res->flags, ST_HEAP)){
			return res;
		}
	}
	PANIC();
	return NULL;
}

u64 sbrk(i64 size){
	//TODO
	auto pd = &thisproc()->pgdir;
	ASSERT(pd);
  //printk("heap in1\n");
	struct section* heap_sec = get_heap_section(pd);
	ASSERT(heap_sec);
	const u64 end = heap_sec->end;
	if(size >= 0){
		heap_sec->end += get_pages_size(size);
	} else {
		ASSERT(heap_sec->begin <= heap_sec->end + size);
		heap_sec->end -= get_pages_size((u64)(-size));
		if(has_flag(heap_sec->flags, ST_SWAP)) {
			for(u64 va = heap_sec->end; va < end ; va += PAGE_SIZE){
				u64* pte = get_pte(pd, va, false);
				if(pte == NULL || *pte == 0) continue;
				if(no_flag(*pte, PTE_VALID))	release_8_blocks(bno_of(pte));
			}
		} else {
			for(u64 va = heap_sec->end; va < end; va += PAGE_SIZE){
				u64* pte = get_pte(pd, va, false);
				if(pte == NULL) continue;
				if(has_flag(*pte, PTE_VALID)){
					void* addr = (void*)P2K(PAGE_BASE(*pte));
					*pte = 0; 
					kfree_page(addr);
				}
			}
		}
	}
	//	printk("in end 1\n");
	arch_tlbi_vmalle1is();
	return end;
}

void init_section(struct section* sec) {
	sec->flags = 0;
	init_sleeplock(&sec->sleeplock);
	sec->begin = 0;
	sec->end = 0;
	init_list_node(&sec->stnode);
}

void* alloc_page_for_user(){
	// 禁用swap

	// while(left_page_cnt() <= REVERSED_PAGES){ //this is a soft limit
	// 	//TODO
 	// 	//printk("alloc swap\n");
	// 	auto p = get_offline_proc();
	// 	if(p == NULL) break;
	// 	auto pd = &p->pgdir;;
	// 	_acquire_spinlock(&pd->lock);
	// 	swapout(pd, get_heap_section(pd));
	// }
	return kalloc_page();
}

void init_sections(ListNode* section_head) {
	init_list_node(section_head);
	//这里图方便建立了所有section但都没有分配实际的内存，只是section本身浪费了空间
	//VA_min-text-data-bss-heap-memory mapping-stack-VA_max

	// stack
    struct section *stack_sec = kalloc(sizeof(struct section));
    init_sleeplock(&stack_sec->sleeplock);
    stack_sec->flags = ST_STACK;
    stack_sec->begin = STACK_BOTTOM;
    stack_sec->end = STACK_BOTTOM;
    _insert_into_list(section_head, &stack_sec->stnode);

	// heap
    struct section *heap_sec = kalloc(sizeof(struct section));
    init_sleeplock(&heap_sec->sleeplock);
    heap_sec->flags = ST_HEAP;
    heap_sec->begin = 0;
    heap_sec->end = 0;
    _insert_into_list(section_head, &heap_sec->stnode);

    // bss&data
    struct section *data_sec = kalloc(sizeof(struct section));
    init_sleeplock(&data_sec->sleeplock);
    data_sec->flags = ST_FILE;
    data_sec->begin = 0;
    data_sec->end = 0;
    _insert_into_list(section_head, &data_sec->stnode);

    // text
    struct section *text_sec = kalloc(sizeof(struct section));
    init_sleeplock(&text_sec->sleeplock);
    text_sec->flags = ST_TEXT;
    text_sec->begin = 0;
    text_sec->end = 0;
    _insert_into_list(section_head, &text_sec->stnode);
}

void free_sections(pgdir* pd) {
	auto head = &pd->section_head;
  	while(!_empty_list(head)){
    	auto node = _detach_from_list(head);
		auto sec = section_of(node);
		if(has_flag(sec->flags, ST_SWAP)){
      		swapin(pd, sec);
		}
		for(u64 va = sec->begin; va < sec->end; va += PAGE_SIZE){
			auto pte = get_pte(pd, va, false);
			if(pte == NULL || *pte == 0) continue;
    		//printk("va:%llx\n", va);
    		//printk("pte:%llx *pte:%llx\n", (u64)pte, *pte);
			kfree_page((void*)P2K(PAGE_BASE(*(u64*)pte)));
			*pte = 0;
		}
		kfree(sec);
  }
}

//caller must have the pd->lock
void swapout(struct pgdir* pd, struct section* st){
	ASSERT(!(st->flags & ST_SWAP));
	st->flags |= ST_SWAP;
	//TODO	
	bool notfile = no_flag(st->flags, ST_FILE);
	u64 begin = st->begin, end = st->end;
	for(u64 va = begin; va < end; va += PAGE_SIZE){
		auto pte = get_pte(pd, va, false);
		if(pte == NULL || no_flag(*pte, PTE_VALID)) continue;
		*pte &= ~PTE_VALID;
	}
	unalertable_wait_sem(&st->sleeplock);
	_release_spinlock(&pd->lock);
	if(notfile) {
		for(u64 va = begin; va < end; va += PAGE_SIZE){
			auto pte = get_pte(pd, va, false);
			if(pte == NULL || *pte == 0) continue;
			//auto pa = PAGE_BASE(*pte);
			auto ka = (void*)P2K(PAGE_BASE(*pte));
			set_bno(pte, write_page_to_disk(ka));
			kfree_page(ka);
		}
	}
	post_sem(&st->sleeplock);
}
//Free 8 continuous disk blocks
void swapin(struct pgdir* pd, struct section* st){
	ASSERT(st->flags & ST_SWAP);
	//TODO
	unalertable_wait_sem(&st->sleeplock);
	auto end = st->end;
	for(u64 va = st->begin; va < end; va += PAGE_SIZE){
		auto pte = get_pte(pd, va, false);
		if(pte == NULL || *pte == 0) {
			continue;
		}
		auto ka = alloc_page_for_user();
		read_page_from_disk(ka, bno_of(pte));
		*pte = K2P(ka) | PTE_USER_DATA | PTE_RW;
	}
	post_sem(&st->sleeplock);
	st->flags &= ~ST_SWAP;
}

int pgfault(u64 iss){
	struct proc* p = thisproc();
	struct pgdir* pd = &p->pgdir;
	u64 addr = arch_get_far();
	//TODO
	struct section* sec = NULL;
	printk("pgfault addr=%llx\n", addr);
	//printk("iss[7:0] = %llx\n", iss & 0xff);
	_for_in_list(node, &pd->section_head){
		if(node == &pd->section_head)	continue;
		sec = section_of(node);
		if(sec->begin <= addr && sec->end > addr)	break;
		else	sec = NULL;
	}
	ASSERT(sec);
	u64* pte = get_pte(pd, addr, true);
  //printk("heap in3\n");
	if(has_flag(sec->flags, ST_SWAP) && no_flag(*pte, PTE_VALID)){
		//swap
		//printk("swap\n");
		swapin(pd, sec);
	} else if(*pte == 0) {
		//lazy
		//printk("lazy\n");
		vmmap(pd, addr, alloc_page_for_user(), PTE_RW | PTE_USER_DATA);
	} else if(has_flag(*pte, PTE_RO) && has_flag(*pte, PTE_VALID)){
		//COW
		//printk("COW\n");
		auto ka = alloc_page_for_user();
		auto ka_pg = (void*)P2K(PAGE_BASE(*pte));
		memcpy(ka, ka_pg, PAGE_SIZE);
		vmmap(pd, addr, ka, PTE_RW | PTE_USER_DATA);
		kfree_page(ka_pg);
	} else {
		PANIC();
	}
	
	arch_tlbi_vmalle1is();
	return (int)iss;
}


void copy_sections(ListNode* from_head, ListNode* to_head){
	while (!_empty_list(to_head)){
		_detach_from_list(to_head->next);
	}
    _for_in_list(node, from_head){
        if (node == from_head)	continue;
        struct section* sec = section_of(node);
        struct section* copy = kalloc(sizeof(struct section));
        copy->flags = sec->flags;
		//printk("copysec flags:%llx\n", sec->flags);
        init_sleeplock(&copy->sleeplock);
        copy->begin = sec->begin;
        copy->end = sec->end;
		//printk("copysec begin:%llx, end:%llx\n", copy->begin, copy->end);
        _insert_into_list(to_head, &copy->stnode);
	}
}


struct section* get_section_has_flags(struct pgdir* pgdir, u64 flags){
	_for_in_list(node, &pgdir->section_head){
		if(node == &pgdir->section_head) continue;
		auto res = section_of(node);
		if(has_flag(res->flags, flags)){
			return res;
		}
	}
	return NULL;
}

struct section* get_section_by_flag(struct pgdir* pgdir, u64 flag){
	_for_in_list(node, &pgdir->section_head){
		if(node == &pgdir->section_head) continue;
		auto res = section_of(node);
		if(res->flags == flag){
			return res;
		}
	}
	return NULL;
}

void alloc_stack_sec(struct pgdir *pgdir){
    struct section *sec = get_section_by_flag(pgdir, ST_STACK);
    ASSERT(sec);
	if(sec->begin < sec->end){
		// printk("exec-has stack\n");
		for (u64 va_pg = sec->begin; va_pg < sec->end; va_pg += PAGE_SIZE){
			kfree_page((void*)
				P2K(PAGE_BASE((u64)*get_pte(pgdir, va_pg, false)))
				);
		}
	}
    sec->begin = sec->end - PAGE_SIZE;
    //sec->begin = STACK_TOP;	
    void *ka_page = alloc_page_for_user();
    ASSERT(ka_page);
    vmmap(pgdir, sec->begin, ka_page, PTE_USER_DATA);
}
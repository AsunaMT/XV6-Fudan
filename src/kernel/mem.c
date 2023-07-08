#include <kernel/init.h>
#include <kernel/mem.h>
#include <kernel/slab.h>
#include <fs/cache.h>
#include <common/string.h>

RefCount alloc_page_cnt;
static QueueNode* pages;
static struct page page_info[ALLOC_PAGE_MAX];
static void* zero_page;
static u64 alloc_page_max;

define_early_init(alloc_page_cnt)
{   
    init_rc(&alloc_page_cnt);
}

define_early_init(pages)
{   
    init_spinlock(&mem_lock);
    int alloc_page_max = 0;
    for (u64 p = PAGE_START; p < P2K(PHYSTOP); p += PAGE_SIZE){
	   add_to_queue(&pages, (QueueNode*)p); 
       init_rc(&page_info[alloc_page_max].ref);
       alloc_page_max++;
    }
    _increment_rc(&alloc_page_cnt);
    zero_page = kalloc_page();
    zero_cnt = &page_info[((u64)zero_page - PAGE_START) / PAGE_SIZE].ref;
    _increment_rc(zero_cnt);
    memset(zero_page, 0, PAGE_SIZE);
    //printk("ii%d\n", alloc_page_max);
}

// Allocate: fetch a page from the queue of usable pages.
void* kalloc_page()
{
    _increment_rc(&alloc_page_cnt);
    auto addr = fetch_from_queue(&pages);
    u64 index = ((u64)addr - PAGE_START) / PAGE_SIZE;
    _increment_rc(&page_info[index].ref);
    return addr;
}

// Free: add the page to the queue of usable pages.
void kfree_page(void* p)
{
    u64 index = ((u64)p - PAGE_START) / PAGE_SIZE;
    _decrement_rc(&page_info[index].ref);
    if(page_info[index].ref.count == 0){
        _decrement_rc(&alloc_page_cnt);
        add_to_queue(&pages, (QueueNode*)p);
    }
}

void* kalloc(isize size)
{   
    _acquire_spinlock(&mem_lock);
    void* res = slab_kalloc(size);
    _release_spinlock(&mem_lock);
    return res;
}

void kfree(void* p)
{   
    _acquire_spinlock(&mem_lock);
    slab_kfree(p);
    _release_spinlock(&mem_lock);
}    

u64 left_page_cnt(){
    return alloc_page_max - alloc_page_cnt.count;
}

WARN_RESULT void* get_zero_page(){
    return zero_page;
}

bool check_zero_page(){
    for(u64* i = (u64*)zero_page; i < (u64*)((u64)zero_page + PAGE_SIZE); ++i){
        //printk("%lld \n", *i);
        if(*i) return false;
    }
    return true;
}

u32 write_page_to_disk(void* ka){
    auto bno = find_and_set_8_blocks();
    for(int i = 0; i < 8; i++){
        Block* block = bcache.acquire(bno + i);
        memcpy(block->data, ka + i * BLOCK_SIZE, BLOCK_SIZE);
        bcache.sync(NULL, block);
        bcache.release(block);
    }
    return bno;
}

void read_page_from_disk(void* ka, u32 bno){
    for(int i = 0; i < 8; i++){
        Block* block = bcache.acquire(bno + i);
        memcpy(ka + i * BLOCK_SIZE, block->data, BLOCK_SIZE);
        bcache.release(block);
    }
    release_8_blocks(bno);
}
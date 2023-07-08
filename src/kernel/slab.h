#pragma once

#include <common/defines.h>
#include <aarch64/mmu.h>
#include <common/list.h>
#include <common/spinlock.h>
#include <kernel/printk.h>

#define SLAB_SIZE_ALIGN (((sizeof(slab_t)&7)==0)? sizeof(slab_t) : ((sizeof(slab_t)>>3<<3) + 8))
#define OBJ_HEAD_SIZE sizeof(QueueNode*)
#define SLAB_SIZE_MAX (PAGE_SIZE - SLAB_SIZE_ALIGN)
#define MEM_CACHES_MAX (SLAB_SIZE_MAX/8 + 2)

typedef struct array_cache 
{
    unsigned int avail; //本地对象缓存池中可用的对象数目
    unsigned int limit; //当本地对象缓冲池的空闲对象数目大于limit时就会主动释放batchcount个对象，便于内核回收和销毁slab
    unsigned int batchcount;    //表示当前CPU的本地对象缓冲池array_cache为空时，从共享的缓冲池或者slabs_partial/slabs_free列表中获取对象的数目
    unsigned int touched;   //从缓存池移除一个对象时，将touched置1，而收缩缓存时，将touched置0，表示这个对象缓冲池最近使用过
    void *entry[];  //保存对象的实体，是一个伪数组，其中并没有数组项，只是为了便于访问内存中array_cache实例之后缓存中的各个对象而已
        /*
         * Must have this definition in here for the proper
         * alignment of array_cache. Also simplifies accessing
         * the entries.
         *
         * Entries should not be directly dereferenced as
         * entries belonging to slabs marked pfmemalloc will
         * have the lower bits set SLAB_OBJ_PFMEMALLOC
         */
}array_cache;

typedef struct slab_t
{
    //unsigned int longcolouroff;      //着色区的大小
    ListNode this;       
    void *addr_obj;         //指向对象区的起点
    unsigned int max;       //最多分配obj个数
    unsigned int in_use;    //已分配obj个数
    unsigned int index;     //所在cache下标        
    //QueueNode* used;
    QueueNode* free;
}slab_t;

typedef struct kmem_cache_t 
{   
    struct array_cache array[4];
    unsigned int object_size;
    unsigned int gfporder;
    struct ListNode* slabs_full;
    struct ListNode* slabs_partial;
    struct ListNode* slabs_free;
    //struct kmem_cache_t* next;
}kmem_cache_t;

slab_t* create_slab(unsigned int index);
void* slab_kalloc(isize);
void slab_kfree(void*);

void* slab_alloc_obj(slab_t* _slab);

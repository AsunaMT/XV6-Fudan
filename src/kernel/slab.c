#include <kernel/slab.h>
#include <kernel/init.h>

static kmem_cache_t caches[MEM_CACHES_MAX];

//extern static QueueNode* pages;

void* kalloc_page();
void kfree_page(void*);

define_early_init(slab){
    caches[0].object_size = 2;
    caches[0].slabs_full = NULL;
    caches[0].slabs_partial = NULL;
    caches[0].slabs_free = NULL;
    caches[1].object_size = 4;
    caches[1].slabs_full = NULL;
    caches[1].slabs_partial = NULL;
    caches[1].slabs_free = NULL;
    for(long unsigned int i = 2; i < MEM_CACHES_MAX; i++)
    {
        caches[i].object_size = (i - 1) << 3;
        caches[i].slabs_full = NULL;
        caches[i].slabs_partial = NULL;
        caches[i].slabs_free = NULL;
    }
} 


void* slab_kalloc(isize size){
    int index = 0;
    if(size <= 2){
        size = 2;
    } else if(size <= 4) {
        size = 4;
        index = 1;
    } else if(size <= 8) {
        size = 8;
        index = 2;
    } else if((size & 7) != 0) {
        index = (size >> 3) + 2;
        size = (index - 1) << 3;
    } else{
        index = (size >> 3) + 1;
    }
    if(caches[index].slabs_partial != NULL) {
        return slab_alloc_obj((slab_t*)caches[index].slabs_partial);
    } else if(caches[index].slabs_free != NULL){
        return slab_alloc_obj((slab_t*)caches[index].slabs_free);
    } else {
        slab_t* slab = create_slab(index);
        slab->in_use += 1;
        QueueNode* obj = fetch_from_queue(&(slab->free));
        //obj->next = NULL;
        //add_to_queue(&(slab->used), obj);
        if(slab->free == NULL){
            caches[index].slabs_full = _merge_list(caches[index].slabs_full, (ListNode*)slab);
        } else {
            caches[index].slabs_partial = _merge_list(caches[index].slabs_partial, (ListNode*)slab);
        }
        return obj;  
    }
}

slab_t* create_slab(unsigned int index)
{
    ListNode* node = kalloc_page();
    //ListNode* node =  (ListNode*)fetch_from_queue(&pages);
    init_list_node(node);
    unsigned int size = caches[index].object_size;
    ((slab_t*)node)->addr_obj = (void*)((u64)node + SLAB_SIZE_ALIGN);
    ((slab_t*)node)->max = 0;
    ((slab_t*)node)->in_use = 0;
    ((slab_t*)node)->index = index;
    //((slab_t*)node)->used = NULL;
    ((slab_t*)node)->free = NULL;
    u64 alloc_size = (size < OBJ_HEAD_SIZE) ? (u64)OBJ_HEAD_SIZE : (u64)size;
    for(u64 p = 0; p < (u64)PAGE_SIZE - (u64)SLAB_SIZE_ALIGN - alloc_size; p += alloc_size)
    {   
        add_to_queue(&(((slab_t*)node)->free), (QueueNode*)(((slab_t*)node)->addr_obj + p));
        ((slab_t*)node)->max += 1; 
    }
    return ((slab_t*)node);
}

void* slab_alloc_obj(slab_t* _slab){
    _slab->in_use += 1;
    int index = _slab->index;
    if(_slab->in_use == _slab->max) {
        if(_slab->in_use == 1) {
            //free to full
            caches[index].slabs_free = _detach_from_list((ListNode*)_slab);
        } else {
            //partial to full
            caches[index].slabs_partial = _detach_from_list((ListNode*)_slab);
        }
        caches[index].slabs_full = _merge_list(caches[index].slabs_full, (ListNode*)_slab);
    } else if(_slab->in_use == 1){
        //free to partial
        caches[index].slabs_free = _detach_from_list((ListNode*)_slab);
        caches[index].slabs_partial = _merge_list(caches[index].slabs_partial, (ListNode*)_slab);
    }
    QueueNode* node = fetch_from_queue(&(_slab->free));
    //if(node==NULL) printk("slab:%psize:%d", _slab, caches[_slab->index].object_size);
    //node->next = NULL;
    //add_to_queue(&(_slab->used), node);
    return node;
}

void slab_kfree(void* p){
    slab_t* slab_ = (slab_t*)(((u64)p >> 12) << 12);
    slab_->in_use -= 1;
    int index = slab_->index;
    if(slab_->free == NULL){
        caches[index].slabs_full = _detach_from_list((ListNode*)slab_);
        if(slab_->in_use != 0){
            //full To partial
            caches[index].slabs_partial = _merge_list(caches[index].slabs_partial, (ListNode*)slab_);   
        } 
        else {
            //full To free
            if(caches[index].slabs_free != NULL) kfree_page(slab_);
            else caches[index].slabs_free = _merge_list(caches[index].slabs_free, (ListNode*)slab_);
        }
    } else {
        if(slab_->in_use == 0){
            //partial To free;
            caches[index].slabs_partial = _detach_from_list((ListNode*)slab_);
            if(caches[index].slabs_free != NULL) kfree_page(slab_);
            else caches[index].slabs_free = _merge_list(caches[index].slabs_free, (ListNode*)slab_);
        }
    }
    add_to_queue(&(slab_->free), (QueueNode*)p);
    // QueueNode* temp = slab_->used;
    // //if(temp==NULL) printk("%p\n", (void*)slab_);
    // QueueNode* node = (QueueNode*)(p - OBJ_HEAD_SIZE);
    // if(temp == node){
    //     slab_->used = node->next;
    // } else {
    //     while (temp)
    //     {
    //         if(temp->next == node){
    //             temp->next = node->next;
    //             node->next = NULL;
    //             break;
    //         }
    //         temp = temp->next;
    //     }
    // }
}

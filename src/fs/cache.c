#include <common/bitmap.h>
#include <common/string.h>
#include <fs/cache.h>
#include <kernel/mem.h>
#include <kernel/printk.h>
#include <kernel/proc.h>
#include <kernel/init.h>

static const SuperBlock* sblock;
static const BlockDevice* device;

static SpinLock lock;     // protects block cache.
//static ListNode head;     // the list of all allocated in-memory block.
static LogHeader header;  // in-memory copy of log header block.

static SpinLock bp_lock;
//   NULL <-- head <--> node1 <--> ...
//            /|\                 /| |
//             |                   | |/
//  tail --> nodeN <--> node <--> ...
typedef struct LRUinfo {
    ListNode head;
    ListNode* tail;
    u32 size;
    SpinLock lock;
} LRUinfo;

static LRUinfo lru_info;

// hint: you may need some other variables. Just add them here.
struct LOG {
    /* data */
    SpinLock lock;
    u32 unresolved; // how many FS sys calls are executing.
    bool committing;  // in commit(), please wait.
    Semaphore sem;
    Semaphore endop_sem;
    // Semaphore commit_sem;
    // Semaphore use_sem;
    usize use_num;
} log;

inline static void write_log();
// inline static void write_loghead();
// inline static void read_loghead();
inline static void writeback();
static void init_LRU();
static void init_and_recover_log();

// read the content from disk.
static INLINE void device_read(Block* block) {
    device->read(block->block_no, block->data);
}

// write the content back to disk.
static INLINE void device_write(Block* block) {
    device->write(block->block_no, block->data);
}

// read log header from disk.
static INLINE void read_header() {
    device->read(sblock->log_start, (u8*)&header);
}

// write log header back to disk.
static INLINE void write_header() {
    device->write(sblock->log_start, (u8*)&header);
}

// initialize a block struct.
static void init_block(Block* block) {
    block->block_no = 0;
    init_list_node(&block->node);
    block->acquired = false;
    block->pinned = false;

    init_sleeplock(&block->lock);
    block->valid = false;
    memset(block->data, 0, sizeof(block->data));
}

// see `cache.h`.
static usize get_num_cached_blocks() {
    // TODO
    return lru_info.size;
}

// see `cache.h`.
static Block* cache_acquire(usize block_no) {
    // TODO
    _acquire_spinlock(&lru_info.lock);
    bool find = false;
    struct Block* target = NULL;
    for (ListNode* i_node = lru_info.head.next; i_node != &lru_info.head; i_node = i_node->next) {
        target = container_of(i_node, struct Block, node);
        if(target->block_no == block_no){
            target->acquired = true;
            find = true;
            break;
        }
    }
    if(find){ 
        _release_spinlock(&lru_info.lock);
        unalertable_wait_sem(&target->lock);
        // _lock_sem(&target->lock);
        // ASSERT(_wait_sem(&target->lock, false));
        _acquire_spinlock(&lru_info.lock);
        
        target->acquired = true;
        if(&target->node != lru_info.tail) {
            lru_info.tail->next = &target->node;
            target->node.prev->next = target->node.next;
            target->node.next->prev = target->node.prev;

            target->node.prev = lru_info.tail;
            target->node.next = &lru_info.head;
            lru_info.tail = &target->node;
        }
    } else {
        target = kalloc(sizeof(Block));
        init_block(target);
        if(lru_info.tail == NULL){
            target->node.prev = &lru_info.head;
            target->node.next = &lru_info.head;
            lru_info.head.next = &target->node;
            lru_info.tail = &target->node;
        } else {
            target->node.prev = lru_info.tail;
            target->node.next = &lru_info.head;
            lru_info.tail->next = &target->node;
            lru_info.tail = &target->node;
        }
        lru_info.size++;
        target->block_no = block_no;
        target->acquired = true;
        _release_spinlock(&lru_info.lock);
        unalertable_wait_sem(&target->lock);
        // _lock_sem(&target->lock);
        // ASSERT(_wait_sem(&target->lock, false));
        device_read(target);
        target->valid = true;

        _acquire_spinlock(&lru_info.lock);
        ListNode* del = lru_info.head.next;
        Block* block_del = container_of(del, struct Block, node);
        if(!(lru_info.size <= EVICTION_THRESHOLD || block_del->acquired || block_del->pinned)){
            lru_info.head.next = del->next;
            del->next->prev = &lru_info.head;
            lru_info.size--;
            kfree(block_del);
        }
    }
    //printk("find? %d  this:%d\nheadnext:%d",find,block_no,container_of(lru_info.head.next, struct Block, node)->block_no);
    // for (ListNode* inode = lru_info.head.next; inode->next != &lru_info.head; inode = inode->next) {
    //     Block* x = container_of(inode, struct Block, node);
    //     printk("b%d->", x->block_no);
    // }
    // if(lru_info.tail!=NULL)printk("b%d->b%dB\n", container_of(lru_info.tail, struct Block, node)->block_no,lru_info.tail->next==&lru_info.head);
    _release_spinlock(&lru_info.lock);
    return target;
}

// see `cache.h`.
static void cache_release(Block* block) {
    // TODO
    _acquire_spinlock(&lru_info.lock);
    _lock_sem(&block->lock);
    if(_query_sem(&block->lock) >= 0)   block->acquired = false;
    _post_sem(&block->lock);
    _unlock_sem(&block->lock);
    _release_spinlock(&lru_info.lock);
}

// initialize block cache.
void init_bcache(const SuperBlock* _sblock, const BlockDevice* _device) {
    sblock = _sblock;
    device = _device;

    // TODO
    init_spinlock(&lock);
    init_spinlock(&bp_lock);
    init_LRU();
    init_and_recover_log();
}

// see `cache.h`.
static void cache_begin_op(OpContext* ctx) {
    // TODO
    while (1) {
        _acquire_spinlock(&log.lock);
        if(log.committing) {
            //unalertable_wait_sem(&log.sem);
            _lock_sem(&log.sem);
            _release_spinlock(&log.lock);
            ASSERT(_wait_sem(&log.sem, false));
            continue;
        }
        usize min = sblock->num_log_blocks < LOG_MAX_SIZE ? sblock->num_log_blocks : LOG_MAX_SIZE;
        if(log.use_num + OP_MAX_NUM_BLOCKS > min) {
            //unalertable_wait_sem(&log.sem);
            _lock_sem(&log.sem);
            _release_spinlock(&log.lock);
            ASSERT(_wait_sem(&log.sem, false));
            continue;
        }
        log.unresolved++;
        ctx->rm = OP_MAX_NUM_BLOCKS;
        log.use_num += OP_MAX_NUM_BLOCKS;
        _release_spinlock(&log.lock);
        break;
    }
}

// see `cache.h`.
static void cache_sync(OpContext* ctx, Block* block) {
    // TODO
    if(ctx == NULL) {
        device_write(block);
        return;
    } 
    _acquire_spinlock(&log.lock);
    _acquire_spinlock(&lru_info.lock);
    block->pinned = true;
    bool unfind = true;
    for (usize i = 0; i < header.num_blocks; i++) {
        if(header.block_no[i] == block->block_no){
            unfind = false;
            break;
        }
    } 
    if(unfind){
        header.block_no[header.num_blocks] = block->block_no;
        header.num_blocks++;
        if(ctx->rm == 0) {
            _release_spinlock(&lru_info.lock);
            _release_spinlock(&log.lock);
            PANIC();
        }
        ctx->rm--;
    }
    _release_spinlock(&lru_info.lock);
    _release_spinlock(&log.lock); 
}

// see `cache.h`.
static void cache_end_op(OpContext* ctx) {
    // TODO
    _acquire_spinlock(&log.lock);
    log.use_num -= ctx->rm;
    log.unresolved--;
    if(log.unresolved == 0) {
        //commit
        log.committing = true;
        _release_spinlock(&log.lock);
        write_log();
        write_header();
        writeback();
        header.num_blocks = 0;
        write_header();
        _acquire_spinlock(&log.lock);
        log.committing = false;
        log.use_num = 0;
        _release_spinlock(&log.lock);
        post_all_sem(&log.sem);
        post_all_sem(&log.endop_sem);
    } else {
        post_all_sem(&log.sem);
        _release_spinlock(&log.lock);
        unalertable_wait_sem(&log.endop_sem);
    }
}


inline static void write_log() {
    for (usize i = 0; i < header.num_blocks; i++) {
        Block* log_block = cache_acquire(sblock->log_start + i + 1);
        Block* cache_block = cache_acquire(header.block_no[i]);
        memmove(log_block->data, cache_block->data, BLOCK_SIZE);
        device_write(log_block); 
        cache_release(log_block);
        cache_release(cache_block);
    }
}

// inline static void write_loghead() {
//     Block* block = cache_acquire(sblock->log_start);
//     LogHeader *log_header = (LogHeader*)(block->data);
//     // for (int i = 0; i < header.num_blocks; i++) {
//     //     log_header->block_no[i] = header.block_no[i];
//     // }
//     memmove(log_header->block_no, header.block_no, header.num_blocks * sizeof(usize));
//     log_header->num_blocks = header.num_blocks;
//     device_write(block);
//     cache_release(block);
// }

inline static void writeback() {
    for(usize i = 0; i < header.num_blocks; i++) {
        Block* log_block = cache_acquire(sblock->log_start + i + 1);
        Block* cache_block = cache_acquire(header.block_no[i]);
        memmove(cache_block->data, log_block->data, BLOCK_SIZE);
        device_write(cache_block); 
        cache_block->pinned = false;
        cache_release(log_block);
        cache_release(cache_block);
    }
}

// inline static void read_loghead() {
//     Block* block = cache_acquire(sblock->log_start);
//     LogHeader* log_header = (LogHeader*)(block->data);
//     // for (int i = 0; i < log_header->num_blocks; i++) {
//     //     header.block_no[i] = log_header->block_no[i];
//     // }
//     memmove(header.block_no, log_header->block_no, log_header->num_blocks * sizeof(usize));
//     header.num_blocks = log_header->num_blocks;
//     cache_release(block);
// }

static void init_LRU(){
    lru_info.head.next = &lru_info.head;
    lru_info.head.prev = NULL;
    lru_info.tail = NULL;
    lru_info.size = 0;
    init_spinlock(&lru_info.lock);
}

static void init_and_recover_log() {
    memset(&log, 0, sizeof(struct LOG));
    // init_sem(&log.commit_sem, 0);
    // init_sem(&log.use_sem, 0);
    init_sem(&log.sem, 0);
    init_sem(&log.endop_sem, 0);
    init_spinlock(&log.lock);
    //_acquire_spinlock(&log.lock);
    read_header();
    //if(header.num_blocks>0)
    for(usize i = 0; i < header.num_blocks; i++) {
        Block* log_block = cache_acquire(sblock->log_start + i + 1);
        Block* cache_block = cache_acquire(header.block_no[i]);
        memmove(cache_block->data, log_block->data, BLOCK_SIZE);
        device_write(cache_block);
        cache_release(log_block); 
        cache_release(cache_block);
    }
    header.num_blocks = 0;
    write_header();
    //_release_spinlock(&log.lock);
};

// see `cache.h`.
// hint: you can use `cache_acquire`/`cache_sync` to read/write blocks.
static usize cache_alloc(OpContext* ctx) {
    // TODO
    //_acquire_spinlock(&bp_lock);
    for(u32 i = 0; i < sblock->num_blocks; i += BIT_PER_BLOCK){
        Block* bitmap_block = cache_acquire(sblock->bitmap_start + i / BIT_PER_BLOCK);
        //TODO: Don't alloc swap
        //for(u32 j = 0; j < BIT_PER_BLOCK && i + j < sblock->num_blocks; j++){
        for(u32 j = 0; j < SWAP_START && i + j < sblock->num_blocks; j++){
            u8 buf = 1 << (j % 8);
            if((bitmap_block->data[j / 8] & buf) == 0){  
                bitmap_block->data[j / 8] |= buf;
                cache_sync(ctx, bitmap_block);
                cache_release(bitmap_block);

                Block* target = cache_acquire(i + j);
                memset(target->data, 0, BLOCK_SIZE);
                cache_sync(ctx, target);
                cache_release(target);

                //_release_spinlock(&bp_lock);
                return i + j;
            }
        }
        cache_release(bitmap_block);
    }
    //_release_spinlock(&bp_lock);
    PANIC();
}

// see `cache.h`.
// hint: you can use `cache_acquire`/`cache_sync` to read/write blocks.
static void cache_free(OpContext* ctx, usize block_no) {
    // TODO
    //_acquire_spinlock(&bp_lock);
    Block* bitmap_block = cache_acquire(sblock->bitmap_start + block_no / BIT_PER_BLOCK);
    u32 i = block_no % BIT_PER_BLOCK;
    u8 buf = 1 << (i % 8);
    if((bitmap_block->data[i / 8] & buf) == 0){
        //_release_spinlock(&bp_lock);
        PANIC();
    }
    bitmap_block->data[i / 8] &= ~buf;
    cache_sync(ctx, bitmap_block);
    cache_release(bitmap_block);
    //_release_spinlock(&bp_lock);
}

BlockCache bcache = {
    .get_num_cached_blocks = get_num_cached_blocks,
    .acquire = cache_acquire,
    .release = cache_release,
    .begin_op = cache_begin_op,
    .sync = cache_sync,
    .end_op = cache_end_op,
    .alloc = cache_alloc,
    .free = cache_free,
};

struct Swap{
    bool map[SWAP_SIZE];
    SpinLock lock;
};

static struct Swap swap;

define_early_init(swap){
    memset(swap.map, 0, SWAP_SIZE);
    init_spinlock(&swap.lock);
}

static u32 get_swap_index(u32 bno){
    //ASSERT(bno >= SWAP_START);
    //ASSERT((bno - SWAP_START % 8 == 0));
    int res = (bno - SWAP_START) / 8;
    //ASSERT(res < SWAP_SIZE);
    return res;
}

void release_8_blocks(u32 bno){
    _acquire_spinlock(&swap.lock);
    swap.map[get_swap_index(bno)] = false;
    _release_spinlock(&swap.lock);
}

u32 find_and_set_8_blocks(){
    _acquire_spinlock(&swap.lock);
    for(int i = 0; i < SWAP_SIZE; i++){
        if(!swap.map[i]){
            swap.map[i] = true;
            _release_spinlock(&swap.lock);
            return i * 8 + SWAP_START;
        }
    }
    _release_spinlock(&swap.lock);
    PANIC();
    return -1;
}
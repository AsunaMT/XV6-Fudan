#include <common/string.h>
#include <fs/inode.h>
#include <kernel/console.h>
#include <kernel/mem.h>
#include <kernel/printk.h>
// #include <sys/stat.h>
#include <kernel/sched.h>

#define inode_of(node) (container_of(node, Inode, node))
#define INODEENTRY_SIZE (sizeof(InodeEntry))
#define DIRENTRY_SIZE (sizeof(DirEntry))

// this lock mainly prevents concurrent access to inode list `head`, reference
// count increment and decrement.
static SpinLock lock;
static ListNode head;

static const SuperBlock* sblock;
static const BlockCache* cache;

// return which block `inode_no` lives on.
static INLINE usize to_block_no(usize inode_no) {
    return sblock->inode_start + (inode_no / (INODE_PER_BLOCK));
}

// return the pointer to on-disk inode.
static INLINE InodeEntry* get_entry(Block* block, usize inode_no) {
    return ((InodeEntry*)block->data) + (inode_no % INODE_PER_BLOCK);
}

// return address array in indirect block.
static INLINE u32* get_addrs(Block* block) {
    return ((IndirectBlock*)block->data)->addrs;
}

// initialize inode tree.
void init_inodes(const SuperBlock* _sblock, const BlockCache* _cache) {
    init_spinlock(&lock);
    init_list_node(&head);
    sblock = _sblock;
    cache = _cache;

    if (ROOT_INODE_NO < sblock->num_inodes)
        inodes.root = inodes.get(ROOT_INODE_NO);
    else
        printk("(warn) init_inodes: no root inode.\n");
}

// initialize in-memory inode.
static void init_inode(Inode* inode) {
    init_sleeplock(&inode->lock);
    init_rc(&inode->rc);
    init_list_node(&inode->node);
    inode->inode_no = 0;
    inode->valid = false;
}

// see `inode.h`.
static usize inode_alloc(OpContext* ctx, InodeType type) {
    ASSERT(type != INODE_INVALID);

    // TODO
    for (u64 no = 1; no < sblock->num_inodes; no++){
        Block* block = cache->acquire(to_block_no(no));
        InodeEntry* entry = get_entry(block, no);
        if (entry->type == INODE_INVALID){
            memset(entry, 0, INODEENTRY_SIZE);
            entry->type = type;
            cache->sync(ctx, block);
            cache->release(block);
            return no;
        }

        cache->release(block);
    }
    //printk("can't find free inode!\n");
    PANIC();
    return 0;
}

// see `inode.h`.
static void inode_lock(Inode* inode) {
    ASSERT(inode->rc.count > 0);
    // TODO
    unalertable_wait_sem(&inode->lock);
}

// see `inode.h`.
static void inode_unlock(Inode* inode) {
    ASSERT(inode->rc.count > 0);
    // TODO
    _post_sem(&inode->lock);
}

// see `inode.h`.
static void inode_sync(OpContext* ctx, Inode* inode, bool do_write) {
    // TODO
    usize no = inode->inode_no;
    //Block* block = cache->acquire(sblock->inode_start + no/INODE_PER_BLOCK);
    Block* block = cache->acquire(to_block_no(no));
    InodeEntry* disk_entry = get_entry(block, no);

    if(!inode->valid) {
        memcpy(&inode->entry, disk_entry, INODEENTRY_SIZE);
        cache->release(block);
        inode->valid = true;
    } else if(do_write){
        memcpy(disk_entry, &inode->entry, INODEENTRY_SIZE);
        cache->sync(ctx, block);
        cache->release(block); 
    } else {
        //printc("syncsync");
        PANIC();
    }
}

// see `inode.h`.
static Inode* inode_get(usize inode_no) {
    //if(inode_no <= 0) printk("%llx", inode_no);
    ASSERT(inode_no > 0);
    ASSERT(inode_no < sblock->num_inodes);
    _acquire_spinlock(&lock);
    // TODO

    Inode* res_inode;
    _for_in_list(node, &head){
        if(node == &head) continue;
        res_inode = inode_of(node);
        if(res_inode->inode_no == inode_no && res_inode->valid) {
            _increment_rc(&res_inode->rc);
            _release_spinlock(&lock);
            return res_inode;
        }
    }
    Block* block = cache->acquire(to_block_no(inode_no));
    InodeEntry* disk_entry = get_entry(block, inode_no);
    //ASSERT(disk_entry->type != INODE_INVALID);

    res_inode = kalloc(sizeof(Inode));
    init_inode(res_inode);
    _increment_rc(&res_inode->rc);
    res_inode->inode_no = inode_no;
    //res_inode->valid = true;

    memcpy(&res_inode->entry, disk_entry, INODEENTRY_SIZE);
    res_inode->valid = true;
    _insert_into_list(&head, &res_inode->node);

    cache->release(block);
    _release_spinlock(&lock);

    return res_inode;
}
// see `inode.h`.
static void inode_clear(OpContext* ctx, Inode* inode) {
    // TODO
    for(u64 i = 0; i < INODE_NUM_DIRECT; i++){
        if (inode->entry.addrs[i]){
            cache->free(ctx, inode->entry.addrs[i]);
            inode->entry.addrs[i] = 0;
        }
    }
    if(inode->entry.indirect){
        Block* ind_block = cache->acquire(inode->entry.indirect);
        IndirectBlock* ind_block_data = (IndirectBlock*)ind_block->data;
        for(u64 i = 0; i < INODE_NUM_INDIRECT; i++){
            if(ind_block_data->addrs[i]){
                cache->free(ctx, ind_block_data->addrs[i]);
                //break;
            }
        }
        cache->release(ind_block);
        cache->free(ctx, inode->entry.indirect);
        inode->entry.indirect = 0;
    }
    inode->entry.num_bytes = 0;
    inode_sync(ctx, inode, true);
}

// see `inode.h`.
static Inode* inode_share(Inode* inode) {
    // TODO
    _acquire_spinlock(&lock);
    _increment_rc(&inode->rc);
    _release_spinlock(&lock);
    return inode;
}

// see `inode.h`.
static void inode_put(OpContext* ctx, Inode* inode) {
    // TODO
    _acquire_spinlock(&lock);
    _decrement_rc(&inode->rc);
    if (!inode->rc.count){
        if(!inode->entry.num_links){
            unalertable_wait_sem(&inode->lock);
            _release_spinlock(&lock);

            inode_clear(ctx, inode);
            u64 no = inode->inode_no;
            Block* block = cache->acquire(to_block_no(no));
            InodeEntry *disk_entry = get_entry(block, no);
            memset(disk_entry, 0, INODEENTRY_SIZE);
            cache->sync(ctx, block);
            cache->release(block);
            _post_sem(&inode->lock);
            _acquire_spinlock(&lock);
        }
        _detach_from_list(&inode->node);
        kfree(inode);
    }
    // if (!inode->rc.count){
    //     _detach_from_list(&inode->node);
    //     kfree(inode);
    // }
    _release_spinlock(&lock);
}

// this function is private to inode layer, because it can allocate block
// at arbitrary offset, which breaks the usual file abstraction.
//
// retrieve the block in `inode` where offset lives. If the block is not
// allocated, `inode_map` will allocate a new block and update `inode`, at
// which time, `*modified` will be set to true.
// the block number is returned.
//
// NOTE: caller must hold the lock of `inode`.
static usize inode_map(OpContext* ctx,
                       Inode* inode,
                       usize offset,
                       bool* modified) {
    // TODO
    u64 index = offset / BLOCK_SIZE;
    *modified = false;
    if(index < INODE_NUM_DIRECT) {
        if(!inode->entry.addrs[index]) {
            inode->entry.addrs[index] = cache->alloc(ctx);
            *modified = true;
        }
        return inode->entry.addrs[index];
    }
    index -= INODE_NUM_DIRECT;

    u64 ind_no = inode->entry.indirect;
    if(!ind_no){
        inode->entry.indirect = ind_no = cache->alloc(ctx);
    }
    Block *ind_block = cache->acquire(ind_no);
    //ASSERT(index < INODE_NUM_INDIRECT);

    IndirectBlock *ind_block_data = (IndirectBlock*)ind_block->data;
    if(!ind_block_data->addrs[index]){
        ind_block_data->addrs[index] = cache->alloc(ctx);
        *modified = true;
    }
    cache->sync(ctx, ind_block);
    cache->release(ind_block);
    return ind_block_data->addrs[index];
    //return res;
}

// see `inode.h`.
static usize inode_read(Inode* inode, u8* dest, usize offset, usize count) {
    InodeEntry* entry = &inode->entry;
    if (inode->entry.type == INODE_DEVICE) {
        ASSERT(inode->entry.major == 1);
        return console_read(inode, (char*)dest, count);
    }
    if (count + offset > entry->num_bytes)
        count = entry->num_bytes - offset;
    usize end = offset + count;
    ASSERT(offset <= entry->num_bytes);
    ASSERT(end <= entry->num_bytes);
    ASSERT(offset <= end);

    // TODO
    if(end == offset)  return 0;

    u64 r_bytes = 0;
    u64 read_begin = offset / BLOCK_SIZE, read_end = (end - 1) / BLOCK_SIZE;
    u64 block_begin = offset % BLOCK_SIZE, block_end = BLOCK_SIZE;
    for(u64 index = read_begin; index <= read_end; index++){
        if(index == read_end){
            block_end = (end - 1) % BLOCK_SIZE + 1;
        }
        bool modified;
        u64 bno = inode_map(NULL, inode, index * BLOCK_SIZE, &modified);
        Block* block = cache->acquire(bno);
        memcpy((void*)(dest + r_bytes), &block->data[block_begin], block_end - block_begin);
        cache->release(block);
        r_bytes += block_end - block_begin;
        block_begin = 0;
    }
    return r_bytes;
}

// see `inode.h`.
static usize inode_write(OpContext* ctx,
                         Inode* inode,
                         u8* src,
                         usize offset,
                         usize count) {
    InodeEntry* entry = &inode->entry;
    usize end = offset + count;
    if (inode->entry.type == INODE_DEVICE) {
        ASSERT(inode->entry.major == 1);
        return console_write(inode, (char*)src, count);
    }
    ASSERT(offset <= entry->num_bytes);
    ASSERT(end <= INODE_MAX_BYTES);
    ASSERT(offset <= end);

    // TODO
    if(offset == end)  return 0;

    u64 w_bytes = 0;
    u64 write_begin = offset / BLOCK_SIZE, write_end = (end - 1) / BLOCK_SIZE;
    u64 block_begin = offset % BLOCK_SIZE, block_end = BLOCK_SIZE;
    for(u64 index = write_begin; index <= write_end; index++){
        if(index == write_end){
            block_end = (end - 1) % BLOCK_SIZE + 1;
        }
        bool modified;
        u64 bno = inode_map(ctx, inode, index * BLOCK_SIZE, &modified);
        Block* block = cache->acquire(bno);
        memcpy(&block->data[block_begin], (src + w_bytes), block_end - block_begin);
        cache->sync(ctx, block);
        cache->release(block);
        w_bytes += block_end - block_begin;
        block_begin = 0;
    }
    if(end > inode->entry.num_bytes){
        inode->entry.num_bytes = end;
    }
    inode_sync(ctx, inode, true);
    return w_bytes;
}

// see `inode.h`.
static usize inode_lookup(Inode* inode, const char* name, usize* index) {
    InodeEntry* entry = &inode->entry;
    ASSERT(entry->type == INODE_DIRECTORY);

    // TODO
    for(u64 offset = 0; offset < entry->num_bytes; offset += DIRENTRY_SIZE){
        DirEntry dir;
        inode_read(inode, (u8*)&dir, offset, DIRENTRY_SIZE);
        if(!dir.inode_no) continue;
        else if(strncmp(name, dir.name, FILE_NAME_MAX_LENGTH)) continue;
        else if(index){
            /*经测试，此处index返回什么偏移量需要inode_remove中有对应的处理，
             *关键在于write的offset应该传字节的偏移量，remove中注意即可
             *(别的地方应该不影响)
             */
            //*index = offset / DIRENTRY_SIZE;
            *index = offset;
        }
        return dir.inode_no;
    }

    return 0;
}

// see `inode.h`.
static usize inode_insert(OpContext* ctx,
                          Inode* inode,
                          const char* name,
                          usize inode_no) {
    InodeEntry* entry = &inode->entry;
    ASSERT(entry->type == INODE_DIRECTORY);

    // TODO
    if(inode_lookup(inode, name, NULL)) return -1;

    u64 offset = 0;
    for( ; offset < entry->num_bytes; offset += DIRENTRY_SIZE){
        DirEntry can_read;
        inode_read(inode, (u8*)&can_read, offset, DIRENTRY_SIZE);
        if(!can_read.inode_no)   break;
    }
    DirEntry dir;
    dir.inode_no = inode_no;
    strncpy(dir.name, name, FILE_NAME_MAX_LENGTH);
    inode_write(ctx, inode, (u8*)&dir, offset, DIRENTRY_SIZE);
    return dir.inode_no;
}

// see `inode.h`.
static void inode_remove(OpContext* ctx, Inode* inode, usize index) {
    // TODO
    DirEntry dir;
    memset(&dir, 0, DIRENTRY_SIZE);

    //inode_write(ctx, inode, (u8*)&dir, index * DIRENTRY_SIZE, DIRENTRY_SIZE);
    inode_write(ctx, inode, (u8*)&dir, index, DIRENTRY_SIZE);
    while(inode->entry.num_bytes == index + DIRENTRY_SIZE){
        inode_read(inode, (u8*)&dir, index, DIRENTRY_SIZE);
        if(!dir.inode_no){
            inode->entry.num_bytes -= DIRENTRY_SIZE;
        }
        if(index){
            index -= DIRENTRY_SIZE;
        }
    }
}

InodeTree inodes = {
    .alloc = inode_alloc,
    .lock = inode_lock,
    .unlock = inode_unlock,
    .sync = inode_sync,
    .get = inode_get,
    .clear = inode_clear,
    .share = inode_share,
    .put = inode_put,
    .read = inode_read,
    .write = inode_write,
    .lookup = inode_lookup,
    .insert = inode_insert,
    .remove = inode_remove,
};
/* Paths. */

/* Copy the next path element from path into name.
 *
 * Return a pointer to the element following the copied one.
 * The returned path has no leading slashes,
 * so the caller can check *path=='\0' to see if the name is the last one.
 * If no name to remove, return 0.
 *
 * Examples:
 *   skipelem("a/bb/c", name) = "bb/c", setting name = "a"
 *   skipelem("///a//bb", name) = "bb", setting name = "a"
 *   skipelem("a", name) = "", setting name = "a"
 *   skipelem("", name) = skipelem("////", name) = 0
 */
static const char* skipelem(const char* path, char* name) {
    const char* s;
    int len;

    while (*path == '/')
        path++;
    if (*path == 0)
        return 0;
    s = path;
    while (*path != '/' && *path != 0)
        path++;
    len = path - s;
    if (len >= FILE_NAME_MAX_LENGTH)
        memmove(name, s, FILE_NAME_MAX_LENGTH);
    else {
        memmove(name, s, len);
        name[len] = 0;
    }
    while (*path == '/')
        path++;
    return path;
}

/* Look up and return the inode for a path name.
 *
 * If parent != 0, return the inode for the parent and copy the final
 * path element into name, which must have room for DIRSIZ bytes.
 * Must be called inside a transaction since it calls iput().
 */
static Inode* namex(const char* path,
                    int nameiparent,
                    char* name,
                    OpContext* ctx) {
    /* TODO: Lab10 Shell */
    Inode* res;
    if(*path == '/') {
        res = inode_get(ROOT_INODE_NO);
    } else {
        res = inode_share(thisproc()->cwd);
    }
        
    while((path = skipelem(path, name)) != NULL){
        inode_lock(res);
        if(nameiparent && *path == '\0'){
            inode_unlock(res);
            return res;
        }

        if(res->entry.type != INODE_DIRECTORY) {
            inode_unlock(res);
            inode_put(ctx, res);
            return NULL;
        }

        usize next_no = inode_lookup(res, name, NULL);
        if(next_no == 0){
            inode_unlock(res);
            inode_put(ctx, res);
            return NULL;
        }
    if(next_no <= 0) printk("%llx", next_no);
        auto next = inode_get(next_no);
        inode_unlock(res);
        inode_put(ctx, res);
        res = next;
    }

    if(nameiparent){
        inode_put(ctx, res);
        return NULL;
    }
    return res;
}

Inode* namei(const char* path, OpContext* ctx) {
    char name[FILE_NAME_MAX_LENGTH];
    return namex(path, 0, name, ctx);
}

Inode* nameiparent(const char* path, char* name, OpContext* ctx) {
    return namex(path, 1, name, ctx);
}

/*
 * Copy stat information from inode.
 * Caller must hold ip->lock.
 */
void stati(Inode* ip, struct stat* st) {
    st->st_dev = 1;
    st->st_ino = ip->inode_no;
    st->st_nlink = ip->entry.num_links;
    st->st_size = ip->entry.num_bytes;
    switch (ip->entry.type) {
        case INODE_REGULAR:
            st->st_mode = S_IFREG;
            break;
        case INODE_DIRECTORY:
            st->st_mode = S_IFDIR;
            break;
        case INODE_DEVICE:
            st->st_mode = 0;
            break;
        default:
            PANIC();
    }
}

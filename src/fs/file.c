/* File descriptors */

#include "file.h"
#include <common/defines.h>
#include <common/spinlock.h>
#include <common/sem.h>
#include <fs/inode.h>
#include <common/list.h>
#include <common/string.h>
#include <kernel/mem.h>
#include "fs.h"
#include <fs/pipe.h>
#include <kernel/printk.h>

static struct ftable ftable;

void init_ftable() {
    // TODO: initialize your ftable
    memset(ftable.file, 0, NFILE * sizeof(File));
    init_spinlock(&ftable.lock);
}

void init_oftable(struct oftable *oftable) {
    // TODO: initialize your oftable for a new process
    memset(oftable, 0, sizeof(struct oftable));
}


void free_oftable(struct oftable* oftable){
    for(int i = 0; i < NOFILE; i++){
        auto f = oftable->ofd[i];
        if(f != NULL){
            fileclose(f);
        }
    }
}

/* Allocate a file structure. */
struct file* filealloc() {
    /* TODO: Lab10 Shell */
    _acquire_spinlock(&ftable.lock);
    for(int i = 0; i < NFILE; i++){
        auto f = &ftable.file[i];
        if(f->ref == 0){
            f->ref = 1;
            _release_spinlock(&ftable.lock);
            return f;
        }
    }
    _release_spinlock(&ftable.lock);
    return 0;
}

/* Increment ref count for file f. */
struct file* filedup(struct file* f) {
    /* TODO: Lab10 Shell */
    _acquire_spinlock(&ftable.lock);
    if(f->ref < 1){
        PANIC();
    }
    f->ref++;
    _release_spinlock(&ftable.lock);
    return f;
}

/* Close file f. (Decrement ref count, close when reaches 0.) */
void fileclose(struct file* f) {
    /* TODO: Lab10 Shell */
    // File f_file;
    // _acquire_spinlock(&ftable.lock);
    // if(f->ref < 1){
    //     PANIC();
    // }
    // if(--f->ref > 0){
    //     _release_spinlock(&ftable.lock);
    //     return;
    // }
    // f_file = *f;
    // f->ref = 0;
    // f->type = FD_NONE;
    // _release_spinlock(&ftable.lock);

    // if(f_file.type == FD_PIPE){
    //     pipeClose(f_file.pipe, f_file.writable);
    // } else if(f_file.type == FD_INODE){
    //     OpContext ctx;
    //     bcache.begin_op(&ctx);
    //     inodes.put(&ctx, f_file.ip);
    //     bcache.end_op(&ctx);
    // }
    _acquire_spinlock(&ftable.lock);
    if(f->ref < 1)  PANIC();
    if(--f->ref > 0){
        _release_spinlock(&ftable.lock);
        return;
    }
    // close
    File f_record = *f;
    f->ref = 0;
    f->type = FD_NONE;
    _release_spinlock(&ftable.lock);

   if(f_record.type == FD_INODE){
        OpContext ctx;
        bcache.begin_op(&ctx);
        inodes.put(&ctx, f_record.ip);
        bcache.end_op(&ctx);
    } else if(f_record.type == FD_PIPE){
        pipeClose(f_record.pipe, f_record.writable);
    }
}

/* Get metadata about file f. */
int filestat(struct file* f, struct stat* st) {
    /* TODO: Lab10 Shell */
    if(f->type == FD_INODE){
        inodes.lock(f->ip);
        stati(f->ip, st);
        inodes.unlock(f->ip);
        return 0;
    }
    return -1;
}

/* Read from file f. */
isize fileread(struct file* f, char* addr, isize n) {
    /* TODO: Lab10 Shell */
    if(f->readable == 0){
        return -1;
    }
    if(f->type == FD_INODE){
        inodes.lock(f->ip);
        int bytes = inodes.read(f->ip, (u8*)addr, f->off, n);
        if(bytes > 0){
            f->off += bytes;
        }
        inodes.unlock(f->ip);
        return bytes;
    } else if(f->type == FD_PIPE){
        return pipeRead(f->pipe, (u64)addr, n);
    }
    //can't read none
    PANIC();
    return -1;
}

/* Write to file f. */
isize filewrite(struct file* f, char* addr, isize n) {
    /* TODO: Lab10 Shell */
    if(f->writable == 0){
        return -1;
    }

    if(f->type == FD_INODE){
        //网上的太复杂，此处图方便直接设置一次最多写一个block的大小
        const int max_size = BLOCK_SIZE;
        //int writen = 0;
        //while(writen < n){
            int to_w = n ;
            if(to_w > max_size){
                to_w = max_size;
            }
            OpContext ctx;
            bcache.begin_op(&ctx);
            inodes.lock(f->ip);
            int bytes = inodes.write(&ctx, f->ip, (u8*)(addr), f->off, to_w);
            if (bytes > 0){
                f->off += bytes;
            }
            inodes.unlock(f->ip);
            bcache.end_op(&ctx);
            ASSERT(bytes == to_w);
            //writen += bytes;
        //}
        return to_w;
    } else if(f->type == FD_PIPE){
        return pipeWrite(f->pipe, (u64)addr, n);
    }
    PANIC();
    return 0;
}

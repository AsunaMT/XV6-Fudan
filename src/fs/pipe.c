#include <kernel/mem.h>
#include <kernel/sched.h>
#include <fs/pipe.h>
#include <common/string.h>
#include <common/sem.h>
#include <kernel/printk.h>

static void init_pipe(Pipe* p){
    p->readopen = 1;
    p->writeopen = 1;
    p->nwrite = 0;
    p->nread = 0;
    init_spinlock(&p->lock);
    init_sem(&p->wlock, 0);
    init_sem(&p->rlock, 0);
}

int pipeAlloc(File** f0, File** f1) {
    // TODO
    Pipe* pipe_alloc = (Pipe*)kalloc(sizeof(Pipe));
    if(pipe_alloc == NULL){
        //PANIC();
        return -1;
    }
    *f0 = filealloc();
    *f1 = filealloc();
    if(*f0){
        fileclose(*f0);
        return -1;
    }
    if(*f1){
        fileclose(*f1);
        return -1;
    }
    init_pipe(pipe_alloc);
    // 读端
    (*f0)->type = FD_PIPE;
    (*f0)->readable = 1;
    (*f0)->writable = 0;
    (*f0)->pipe = pipe_alloc;
    // 写端
    (*f1)->type = FD_PIPE;
    (*f1)->readable = 0;
    (*f1)->writable = 1;
    (*f1)->pipe = pipe_alloc;
    return 0;
}

void pipeClose(Pipe* pi, int writable) {
    // TODO
    _acquire_spinlock(&pi->lock);
    if(writable) {
        pi->writeopen = 0;
        post_sem(&pi->rlock);
    } else {
        pi->readopen = 0;
        post_sem(&pi->wlock);
    }
    if(pi->readopen == 0 && pi->writeopen == 0) {
        _release_spinlock(&pi->lock);
        kfree((void*)pi);
    } else {
        _release_spinlock(&pi->lock);
    }
}

int pipeWrite(Pipe* pi, u64 addr, int n) {
    // TODO
    _acquire_spinlock(&pi->lock);
    for(int i = 0; i < n; i++) {
        // wait
        while(pi->nwrite == pi->nread + PIPESIZE) {
            if(pi->readopen == 0 || thisproc()->killed) {
                _release_spinlock(&pi->lock);
                return -1;
            }
            // TODO: 清空写资源，写进程睡眠，然后post让读
            get_all_sem(&pi->wlock);
            post_all_sem(&pi->rlock);
            _lock_sem(&pi->wlock);
            _release_spinlock(&pi->lock);
            ASSERT(_wait_sem(&pi->wlock, false));
            _acquire_spinlock(&pi->lock);
        }
        pi->data[pi->nwrite++ % PIPESIZE] = ((char*)addr)[i];
    }
    post_all_sem(&pi->rlock);
    _release_spinlock(&pi->lock);
    return n;
}

int pipeRead(Pipe* pi, u64 addr, int n) {
    // TODO
    _acquire_spinlock(&pi->lock);
    while(pi->nread == pi->nwrite && pi->writeopen) {
        if (thisproc()->killed) {
            _release_spinlock(&pi->lock);
            return -1;
        }
        get_all_sem(&pi->rlock);
        _lock_sem(&pi->rlock);
        _release_spinlock(&pi->lock);
        ASSERT(_wait_sem(&pi->rlock, false));
        //_wait_sem(&pi->rlock, false);
        _acquire_spinlock(&pi->lock);
    }
    int i = 0;
    for(; i < n; i++) { 
        if(pi->nread == pi->nwrite){
            break;
        }
        ((char*)addr)[i] = pi->data[pi->nread++ % PIPESIZE];
    }
    post_all_sem(&pi->wlock);
    _release_spinlock(&pi->lock);
    return i;
}
#include <kernel/pid.h>
#include <common/bit_op.h>

static pids_info pid_map;

int alloc_pid(){
    return bitmap_fetch0(pid_map.pid_bitmap, MAX_PROC_NUM) + 1;
}

void free_pid(int pid){
    bitmap_reset(pid_map.pid_bitmap, pid - 1);
}

int alloc_pid_bp(struct pids_info* bitmap){
    return bitmap_fetch0(bitmap->pid_bitmap, MAX_PROC_NUM) + 1;
}

void free_pid_bp(struct pids_info* bitmap, int local_pid){
    ASSERT(local_pid > 0 && local_pid <= MAX_PROC_NUM);
    bitmap_reset(bitmap->pid_bitmap, local_pid - 1);
}
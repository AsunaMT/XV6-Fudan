#pragma once

#include <common/defines.h>

#define MAX_PROC_NUM 512

typedef struct pids_info{
    bool pid_bitmap[MAX_PROC_NUM];    
}pids_info;

int alloc_pid();
void free_pid(int pid);
int alloc_pid_bp(struct pids_info* bitmap);
void free_pid_bp(struct pids_info* bitmap, int local_pid);
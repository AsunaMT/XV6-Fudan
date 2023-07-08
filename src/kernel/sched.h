#pragma once

#include <kernel/proc.h>
#include <kernel/schinfo.h>
#include <kernel/cpu.h>
#define RR_TIME 1000

void init_schinfo(struct schinfo*, bool group);
void init_schqueue(struct schqueue*);

bool _activate_proc(struct proc*, bool onalert);
#define activate_proc(proc) _activate_proc(proc, false)
#define alert_proc(proc) _activate_proc(proc, true)
WARN_RESULT bool is_zombie(struct proc*);
WARN_RESULT bool is_unused(struct proc*);
void _acquire_sched_lock();
void _release_sched_lock();
#define lock_for_sched(checker) (checker_begin_ctx(checker), _acquire_sched_lock())
void _sched(enum procstate new_state);
// MUST call lock_for_sched() before sched() !!!
#define sched(checker, new_state) (checker_end_ctx(checker), _sched(new_state))
#define yield() (_acquire_sched_lock(), _sched(RUNNABLE))

WARN_RESULT struct proc* thisproc();

// typedef struct cfs_rq {
// 	//struct load_weight load;							/* 就绪队列的总权重 */
// 	unsigned int nr_running;				            /* 可运行状态的进程数量*/

// 	//u64 exec_clock;										/* 统计就绪队列总的运行时间 */
// 	u64 min_vruntime;									/* 用于跟踪CFS就绪队列中红黑树最小vruntime */

// 	struct rb_root_ rq_rb_root;						/* 红黑树的根 */
// 	rb_node rb_leftmost;						/* 指向红黑树中最左边的调度实体 */
// 	//struct schinfo *curr, *next, *last, *skip;		    /* 正在运行的进程，切换的下一个进程 */
// }cfs_rq;

// void init_csf_rq();
struct proc* proc_of(struct schinfo *sinfo);
struct schinfo* schinfo_of(rb_node node);
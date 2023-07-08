#pragma once

#include <common/list.h>
#include <common/rbtree.h>

struct proc; // dont include proc.h here

// embedded data for cpus
struct sched
{
    // TODO: customize your sched info
    struct proc* thisproc;
    struct proc* idle;
};

// embeded data for procs
struct schinfo
{
    // TODO: customize your sched info
    struct rb_node_ run_node;
    unsigned int    on_rq;  
    int	load_weight;                /* 调度实体的权重 */
    u64 exec_start;					/* 调度实体虚拟时间的起始时间 */
	u64	sum_exec_runtime;			/* 调度实体总的运行时间，实际时间 */
	u64	vruntime;					/* 调度实体的虚拟时间 */
	//u64	prev_sum_exec_runtime;		/* 上一次统计调度实体运行总时间 */
    bool isgroup;
};

// embedded data for containers
struct schqueue
{
    // TODO: customize your sched queue
    //struct load_weight load;							/* 就绪队列的总权重 */
	unsigned int nr_running;				            /* 可运行状态的进程数量*/

	u64 exec_clock;										/* 统计就绪队列总的运行时间 */
	u64 min_vruntime;									/* 用于跟踪CFS就绪队列中红黑树最小vruntime */

	struct rb_root_ rq_rb_root;						/* 红黑树的根 */
	rb_node rb_leftmost;						/* 指向红黑树中最左边的调度实体 */
	//struct schinfo *curr, *next, *last, *skip;		    /* 正在运行的进程，切换的下一个进程 */
    //SpinLock rqlock;
};

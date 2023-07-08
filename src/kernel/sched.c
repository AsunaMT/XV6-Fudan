#include <kernel/sched.h>
#include <kernel/proc.h>
#include <kernel/init.h>
#include <kernel/mem.h>
#include <kernel/printk.h>
#include <kernel/container.h>
#include <aarch64/intrinsic.h>
#include <kernel/cpu.h>
#include <driver/clock.h>
#include <common/string.h>
#include <common/rbtree.h>

extern bool panic_flag;
extern struct container root_container;

extern void swtch(KernelContext* new_ctx, KernelContext** old_ctx);

static SpinLock rqlock;
//static cfs_rq rq;
//static ListNode rq;

static void update_vruntime(struct proc *p);
static struct schinfo* fetch_next_schinfo(struct container* cont);
// static struct schinfo* fetch_next_schinfo(struct schqueue *rq);
static void add_runnable_proc(struct proc *prev);
//static void set_next_entity(struct schinfo *se);
static bool compare_schinfo_rb_node(rb_node lnode, rb_node rnode);

// void init_csf_rq(){
//     //memset(rq, 0, sizeof(*rq));
// }

void init_schqueue(struct schqueue* schq){
    memset(schq, 0, sizeof(struct schqueue));
    //init_spinlock(&schq->rqlock);
}

define_early_init(rq)
{
    init_spinlock(&rqlock);
    //init_csf_rq();
}

define_init(sched)
{   
    for(int i = 0; i < NCPU; i++)
    {
        struct proc* p = kalloc(sizeof(struct proc));
        p->idle = 1;
        p->pid = 0;
        p->killed = false;
        p->state = RUNNING;
        cpus[i].sched.thisproc = cpus[i].sched.idle = p;
    }
}

struct proc* thisproc()
{
    // TODO: return the current process
    return cpus[cpuid()].sched.thisproc;
}

void init_schinfo(struct schinfo* p, bool group)
{
    // TODO: initialize your customized schinfo for every newly-created process
    memset(p, 0, sizeof(struct schinfo));
    p->load_weight = 1;
    p->isgroup = group;
}

void _acquire_sched_lock()
{
    // TODO: acquire the sched_lock if need
    _acquire_spinlock(&rqlock);
}

void _release_sched_lock()
{
    // TODO: release the sched_lock if need
    _release_spinlock(&rqlock);
}

bool is_zombie(struct proc* p)
{
    bool r;
    _acquire_sched_lock();
    r = p->state == ZOMBIE;
    _release_sched_lock();
    return r;
}

bool is_unused(struct proc* p)
{
    bool r;
    _acquire_sched_lock();
    r = p->state == UNUSED;
    _release_sched_lock();
    return r;
}

bool _activate_proc(struct proc* p, bool onalert)
{
    // TODO
    // if the proc->state is RUNNING/RUNNABLE, do nothing and return false
    // if the proc->state is SLEEPING/UNUSED, set the process state to RUNNABLE, add it to the sched queue, and return true
    // if the proc->state is DEEPSLEEING, do nothing if onalert or activate it if else, and return the corresponding value.
    _acquire_sched_lock();
    if(p->state == RUNNING || p->state == RUNNABLE){
        _release_sched_lock();
        return false;
    }
    if(p->state == SLEEPING || p->state == UNUSED || ((p->state == DEEPSLEEPING) && (!onalert))){
        p->state = RUNNABLE;
        //ASSERT(p != cpus[cpuid()].sched.idle);
        add_runnable_proc(p);
        //printk("act, pid:%d\n", p->pid);
        _release_sched_lock();
        return true;
    }
    _release_sched_lock();
    //PANIC();
    return false;
}

void activate_group(struct container* group)
{
    // TODO: add the schinfo node of the group to the schqueue of its parent
    _acquire_sched_lock();
    group->schinfo.on_rq = 1;
    int ok =  
    _rb_insert(&(group->schinfo.run_node),
     &group->parent->schqueue.rq_rb_root, compare_schinfo_rb_node);
    ASSERT(ok == 0);
    group->parent->schqueue.rb_leftmost = _rb_first(&group->parent->schqueue.rq_rb_root);
    group->parent->schqueue.min_vruntime = 
        ((struct schinfo*)(group->parent->schqueue.rb_leftmost))->vruntime;
    group->parent->schqueue.nr_running++;
    _release_sched_lock();
}

extern struct proc root_proc;
static void update_this_state(enum procstate new_state)
{
    // update the state of current process to new_state, and remove it from the sched queue if new_state=SLEEPING/ZOMBIE
    auto this = thisproc();
    this->state = new_state;
    if(this == cpus[cpuid()].sched.idle) return;
	update_vruntime(this);
}

static void update_vruntime(struct proc *p){
    u64 delta = get_timestamp() - p->schinfo.exec_start;
    p->schinfo.sum_exec_runtime += delta;
    p->schinfo.vruntime += delta * p->schinfo.load_weight;
    auto con = p->container;
    while(con != &root_container){;
        _rb_erase(&con->schinfo.run_node, &con->parent->schqueue.rq_rb_root);
        con->schinfo.sum_exec_runtime += delta;
        con->schinfo.vruntime += delta;
        int ok =  
        _rb_insert(&(con->schinfo.run_node), &con->parent->schqueue.rq_rb_root, 
        compare_schinfo_rb_node);
        ASSERT(ok == 0);
        auto pa_schq = &con->parent->schqueue;
        ASSERT(pa_schq);
        pa_schq->rb_leftmost = _rb_first(&pa_schq->rq_rb_root);
        pa_schq->min_vruntime = ((struct schinfo*)(pa_schq->rb_leftmost))->vruntime;
        con = con->parent;
    }
}

extern bool panic_flag;
static struct proc* pick_next_cfs(struct proc *prev)
//static struct proc* pick_next_cfs()
{
    // choose the next process to run by cfs, and return idle if no runnable process
    if(panic_flag) {
        return cpus[cpuid()].sched.idle;
    }
	struct schinfo *sinfo;
    if((prev->state == RUNNABLE) && (prev != cpus[cpuid()].sched.idle)) {
        add_runnable_proc(prev);  
    }
    sinfo = fetch_next_schinfo(&root_container);
    if(sinfo == NULL) {
        return cpus[cpuid()].sched.idle;
    }
	return proc_of(sinfo);
}

static void update_this_proc(struct proc* p)
{
    // update thisproc to the choosen process, and reset the clock interrupt if need
    if(!sched_timer[cpuid()].triggered) cancel_cpu_timer(&sched_timer[cpuid()]);
    set_cpu_timer(&sched_timer[cpuid()]);
    //reset_clock(1000);
    cpus[cpuid()].sched.thisproc = p;
    p->schinfo.exec_start = get_timestamp();
}

static void cfs_sched(enum procstate new_state)
{   
    //printk("proc_test in\n");
    auto this = thisproc();
    if(this->killed && new_state != ZOMBIE) {
        _release_sched_lock();
        return;
    }
    //printk("proc_test %d idle%d\n", this->pid, this->idle);
    ASSERT(this->state == RUNNING);
    update_this_state(new_state);
    auto next = pick_next_cfs(this);
    update_this_proc(next);
    ASSERT(next->state == RUNNABLE);
    next->state = RUNNING;
    if (next != this)
    {
    //printk("proc_test PASS%d\n", next->pid);
        attach_pgdir(&next->pgdir);
        swtch(next->kcontext, &this->kcontext);
    }
    _release_sched_lock();
}

__attribute__((weak, alias("cfs_sched"))) void _sched(enum procstate new_state);

u64 proc_entry(void(*entry)(u64), u64 arg)
{
    _release_sched_lock();
    set_return_addr(entry);
    return arg;
}

struct proc* proc_of(struct schinfo* sinfo){
    return container_of(sinfo, struct proc, schinfo);
}

struct schinfo* schinfo_of(rb_node node){
    return container_of(node, struct schinfo, run_node);
}

static struct schinfo* fetch_next_schinfo(struct container* cont){
    if(cont->schqueue.nr_running == 0) return NULL;
    rb_node min_node = cont->schqueue.rb_leftmost;
    auto min_node_schinfo = schinfo_of(min_node);
    while(1) {
        if(min_node_schinfo->isgroup){
            cont = container_of(min_node_schinfo, struct container, schinfo);
            if(cont->schqueue.nr_running != 0) {
                auto ret = fetch_next_schinfo(cont);
                if(ret) return ret;
            }
            min_node = _rb_successor(min_node);
            if(!min_node) return NULL;
            min_node_schinfo = schinfo_of(min_node);
        } else {
            cont = proc_of(min_node_schinfo)->container;
            break;
        }
    }
    schinfo_of(min_node)->on_rq = 0;
    _rb_erase(min_node, &cont->schqueue.rq_rb_root);
    cont->schqueue.rb_leftmost = _rb_first(&cont->schqueue.rq_rb_root);
    if(cont->schqueue.rb_leftmost != NULL)
        cont->schqueue.min_vruntime = schinfo_of(cont->schqueue.rb_leftmost)->vruntime;
    else 
        cont->schqueue.min_vruntime = ~0ll;
    cont->schqueue.nr_running--;
    //printk("\nfetch--nr_running:%d,rq_rb_root:%p rb_leftmost:%p min_vruntime:%lld\n", rq.nr_running, rq.rq_rb_root.rb_node, rq.rb_leftmost, rq.min_vruntime);
    return min_node_schinfo;
}

static void add_runnable_proc(struct proc *prev){
    prev->schinfo.on_rq = 1;
    int ok =  
    _rb_insert(&(prev->schinfo.run_node),
     &prev->container->schqueue.rq_rb_root, compare_schinfo_rb_node);
    ASSERT(ok == 0);
    //can be optimized
    prev->container->schqueue.rb_leftmost = _rb_first(&prev->container->schqueue.rq_rb_root);
    prev->container->schqueue.min_vruntime = 
        ((struct schinfo*)(prev->container->schqueue.rb_leftmost))->vruntime;
    prev->container->schqueue.nr_running++;
    //printk("\nadd--to add:%p nr_running:%d rq_rb_root:%p rb_leftmost:%p rb_rootleft:%p min_vruntime:%lld\n",&prev->schinfo.run_node, rq.nr_running, rq.rq_rb_root.rb_node, rq.rb_leftmost, rq.rq_rb_root.rb_node->rb_left, rq.min_vruntime);
}

static bool compare_schinfo_rb_node(rb_node lnode, rb_node rnode){
    if(schinfo_of(lnode)->vruntime == schinfo_of(rnode)->vruntime){
        //ASSERT((u64)lnode != (u64)rnode);
        return (u64)lnode < (u64)rnode;
    }
    return schinfo_of(lnode)->vruntime < schinfo_of(rnode)->vruntime;
}
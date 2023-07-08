#include <kernel/proc.h>
#include <kernel/init.h>
#include <kernel/mem.h>
#include <kernel/sched.h>
#include <common/list.h>
#include <common/string.h>
#include <common/rbtree.h>
#include <kernel/printk.h>
#include <aarch64/intrinsic.h>
#include <kernel/pid.h>
#include <kernel/container.h>
#include <fs/inode.h>
#include "proc.h"

struct proc root_proc;
extern struct container root_container;

void kernel_entry();
void proc_entry();

static SpinLock plock;
//static int pid;

define_early_init(plock)
{
    init_spinlock(&plock);
}

void set_parent_to_this(struct proc* proc)
{
    // TODO: set the parent of proc to thisproc
    // NOTE: maybe you need to lock the process tree
    // NOTE: it's ensured that the old proc->parent = NULL
    _acquire_spinlock(&plock);
    proc->parent = thisproc();
    _insert_into_list(&thisproc()->children, &proc->ptnode);
    _release_spinlock(&plock);
}

NO_RETURN void exit(int code)
{
    // TODO
    // 1. set the exitcode
    // 2. clean up the resources
    // 3. transfer children to the rootproc of the container, and notify the it if there is zombie
    // 4. notify the parent
    // 5. sched(ZOMBIE)
    // NOTE: be careful of concurrency
    _acquire_spinlock(&plock);
    struct proc* this = thisproc();
    ASSERT(this != this->container->rootproc && !this->idle);
    //printk("exit:%d\n", this->pid);
    this->exitcode = code;
    while(!_empty_list(&this->children))
    {   
        ListNode* child = this->children.next;
        _detach_from_list(child);
        auto child_proc = container_of(child, struct proc, ptnode);
        // child_proc->parent = &root_proc; 
        struct proc* root = this->container->rootproc;
        child_proc->parent = root; 
        // _insert_into_list(&root_proc.children, &child_proc->ptnode);
        _insert_into_list(&root->children, &child_proc->ptnode);
        if(is_zombie(child_proc)) post_sem(&root->childexit);
    }
    post_sem(&(this->parent->childexit));
    //arch_fence();
    free_pgdir(&this->pgdir);
    _acquire_sched_lock();
    _release_spinlock(&plock);
    _sched(ZOMBIE);

    PANIC(); // prevent the warning of 'no_return function returns'
}

int wait(int* exitcode, int* pid)
{
    // TODO
    // 1. return -1 if no children
    // 2. wait for childexit
    // 3. if any child exits, clean it up and return its local pid and exitcode
    // NOTE: be careful of concurrency
    struct proc* this = thisproc();
    //printk("\nwait:%d\n", this->pid);
    if(_empty_list(&this->children))    return -1;

    if(!wait_sem(&this->childexit)) PANIC();
    //while(1){
        
    _acquire_spinlock(&plock);
    _for_in_list(child, &this->children)
    {   
        if(child == &this->children) continue;
        auto child_proc = container_of(child, struct proc, ptnode);
        if(is_zombie(child_proc)){
            _detach_from_list(child);
            *exitcode = child_proc->exitcode; 
            int local_pid = child_proc->localpid;
            *pid = child_proc->pid;
            //printk("child_proc->pid=%d\n",child_proc->pid);
            _acquire_spinlock(&child_proc->container->localpid_lock);
            free_pid_bp(&(child_proc->container->local_pid_map), local_pid);
            _release_spinlock(&child_proc->container->localpid_lock);
            free_pid(*pid);
            kfree_page(child_proc->kstack);
            kfree(child_proc);
            //printk("\nwait:%d finish\n", this->pid);   
            _release_spinlock(&plock);
            return local_pid;
        }
    }
    _release_spinlock(&plock);
    //}
    //_release_spinlock(&plock);
    
    PANIC();   
    return 0;
}

struct proc* get_proc(struct proc* p, int pid){
    struct proc* res = NULL;
    _for_in_list(child, &p->children) {
        if(child == &p->children) continue;

        res = container_of(child, struct proc, ptnode);
        if(res->pid == pid) return res;

        res = get_proc(res, pid);
        if(res) return res; 
    }
    return NULL;
}

static struct proc* get_offline_proc_recursion(struct container* cont){
    rb_node max_node = _rb_last(&cont->schqueue.rq_rb_root);
    auto max_node_schinfo = schinfo_of(max_node);
    while(1) {
        if(max_node_schinfo->isgroup){
            cont = container_of(max_node_schinfo, struct container, schinfo);
            if(cont->schqueue.nr_running != 0) {
                auto ret = get_offline_proc_recursion(cont);
                if(ret) return ret;
            }
        } else {
            auto p = proc_of(max_node_schinfo);
            if(!p->pgdir.online) return p;
        }
        max_node = _rb_precursor(max_node);
        if(!max_node) return NULL;
        max_node_schinfo = schinfo_of(max_node);
    }
    return NULL;
}

struct proc* get_offline_proc() {
    return get_offline_proc_recursion(&root_container);
}

int kill(int pid) {
    // TODO
    // Set the killed flag of the proc to true and return 0.
    // Return -1 if the pid is invalid (proc not found).
    _acquire_spinlock(&plock);
    auto p = get_proc(&root_proc, pid);
    if((!p) || is_unused(p)){
        _release_spinlock(&plock);
        return -1;
    }
    p->killed = true;
    alert_proc(p);
    _release_spinlock(&plock);
    return 0;
}

int start_proc(struct proc* p, void(*entry)(u64), u64 arg)
{
    // TODO
    // 1. set the parent to root_proc if NULL
    // 2. setup the kcontext to make the proc start with proc_entry(entry, arg)
    // 3. activate the proc and return its local pid
    // NOTE: be careful of concurrency
    //printk("start, pid:%d\n", p->pid);
    if(p->parent == NULL)
    {
        _acquire_spinlock(&plock);
        p->parent = &root_proc;
        _insert_into_list(&root_proc.children, &p->ptnode);
        _release_spinlock(&plock);
    }
    p->kcontext->lr = (u64)&proc_entry;
    p->kcontext->x0 = (u64)entry;
    p->kcontext->x1 = (u64)arg;
    _acquire_spinlock(&p->container->localpid_lock);
    p->localpid = alloc_pid_bp(&(p->container->local_pid_map));
    _release_spinlock(&p->container->localpid_lock);
    activate_proc(p);
    return p->localpid;
}

void init_proc(struct proc* p)
{
    // TODO
    // setup the struct proc with kstack and pid allocated
    // NOTE: be careful of concurrency
    memset(p, 0, sizeof(*p));
    _acquire_spinlock(&plock);
    //p->pid = ++pid;
    p->pid = alloc_pid();
    //printk("new proc:%p  pid: %d\n", p, p->pid);
    _release_spinlock(&plock);
    init_sem(&p->childexit, 0);
    init_list_node(&p->children);
    init_list_node(&p->ptnode);
    p->kstack = kalloc_page();
    p->state = UNUSED;
    init_schinfo(&p->schinfo, false);
    init_pgdir(&p->pgdir);
    //init_oftable(&p->oftable);
	//printk("addr=\n");
    p->container = &root_container;
    //printk("%p\n", p->container);
    p->kcontext = (KernelContext*)((u64)p->kstack + PAGE_SIZE - 16 - sizeof(KernelContext) - sizeof(UserContext));
    p->ucontext = (UserContext*)((u64)p->kstack + PAGE_SIZE - 16 - sizeof(UserContext));
    if(p != &root_proc) p->cwd = inodes.root;
}

struct proc* create_proc()
{   
    struct proc* p = kalloc(sizeof(struct proc));
    init_proc(p);
	//printk("addr=\n");
    return p;
}

define_init(root_proc)
{   
    init_proc(&root_proc);
    root_proc.parent = &root_proc;
    //printk("hello world!\n");
    //printk("root proc:%p root pid: %d create cpu:%d\n", &root_proc, root_proc.pid, cpuid());
    start_proc(&root_proc, kernel_entry, 123456);
}

/*
 * Create a new process copying p as the parent.
 * Sets up stack to return as if from system call.
 */
void trap_return();
int fork() {
    /* TODO: Your code here. */
    //printk("in fork\n");
    struct proc *parent = thisproc();
    struct proc *child = create_proc();
    ASSERT(child);

    // basic info
    child->killed = parent->killed;
    child->idle = parent->idle;
    set_parent_to_this(child);
    set_container_to_this(child);
    memmove(child->kcontext, parent->kcontext, sizeof(KernelContext));
    memmove(child->ucontext, parent->ucontext, sizeof(UserContext));
    child->ucontext->x[0] = 0;
    child->ucontext->spsr = 0;

    // file
    for(int i = 0; i < NOFILE; i++){
        if(parent->oftable.ofd[i]){
            child->oftable.ofd[i] = filedup(parent->oftable.ofd[i]);
        }
    }
    if(parent->cwd){
        child->cwd = inodes.share(parent->cwd);
    } else {
        child->cwd = NULL;
    }
    ASSERT(copy_pgdir(&parent->pgdir, &child->pgdir) >= 0);
    return start_proc(child, trap_return, 0);
}
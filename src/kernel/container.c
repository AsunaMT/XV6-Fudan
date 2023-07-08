#include <common/string.h>
#include <common/list.h>
#include <kernel/container.h>
#include <kernel/init.h>
#include <kernel/printk.h>
#include <kernel/mem.h>
#include <kernel/sched.h>

struct container root_container;
extern struct proc root_proc;

void activate_group(struct container* group);

void set_container_to_this(struct proc* proc)
{
    proc->container = thisproc()->container;
}

void init_container(struct container* container)
{
    memset(container, 0, sizeof(struct container));
    container->parent = NULL;
    container->rootproc = NULL;
    init_schinfo(&container->schinfo, true);
    init_schqueue(&container->schqueue);
    // TODO: initialize namespace (local pid allocator)
    init_spinlock(&container->localpid_lock);
    _acquire_spinlock(&container->localpid_lock);
    memset(&container->local_pid_map, 0, sizeof(struct pids_info));
    _release_spinlock(&container->localpid_lock);
}

struct container* create_container(void (*root_entry)(), u64 arg)
{
    // TODO
    struct container* new_container = kalloc(sizeof(struct container));
    init_container(new_container);
    new_container->parent = thisproc()->container;
    struct proc* this_rootproc = create_proc();
    set_parent_to_this(this_rootproc);
    new_container->rootproc = this_rootproc;
    this_rootproc->container = new_container;
    
    start_proc(this_rootproc, root_entry, arg);
    activate_group(new_container);    
    return new_container;
}

define_early_init(root_container)
{
    init_container(&root_container);
    root_container.rootproc = &root_proc;
}

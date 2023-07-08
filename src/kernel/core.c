#include <kernel/cpu.h>
#include <kernel/printk.h>
#include <kernel/init.h>
#include <kernel/sched.h>
#include <kernel/paging.h>
#include <test/test.h>
#include <driver/sd.h>
#include <fs/block_device.h>
#include <fs/cache.h>

bool panic_flag;


extern char icode[], eicode[];
extern void trap_return();

NO_RETURN void idle_entry() {
    set_cpu_on();

    while (1) { 
        yield();
        if (panic_flag)
            break;
        arch_with_trap {
            arch_wfi();
        }
        //printk("cpu:%d\n", cpuid());
    }
    set_cpu_off();
    arch_stop_cpu();
}

static void root_creat_userproc(){
    //printk("proc size = %d\n", (int)sizeof(struct proc));
    auto p = create_proc();
    //printk("icode = %llx,eicode = %llx\n", (u64)icode, (u64)eicode);
    // for(u64 i = (u64)icode; i < (u64)eicode; i += 8){
    //     printk("addr = %llx,val = %llx\n", i, *(u64*)i);
    // }
    for (u64 addr = (u64)icode; addr < (u64)eicode; addr += PAGE_SIZE){      
        vmmap(&p->pgdir, 0x400000 + addr - (u64)icode, (void*)addr, PTE_USER_DATA);
    }
    struct section* sec = get_section_by_flag(&p->pgdir, ST_TEXT);
    sec->begin = 0x400000;
    sec->end = 0x400000 + PAGE_UP((u64)eicode - (u64)icode);
    
	// arch_fence();
	// arch_tlbi_vmalle1is();
	// arch_fence();
    //auto from_head = &p->pgdir.section_head;
    //printk("create userproc, pid = %d\n", p->pid);
    // _for_in_list(node, from_head){
    //     if (node == from_head)	continue;
    //     struct section* sec = section_of(node);
	// 	printk("sec flags:%llx\n", sec->flags);
	// 	printk("sec begin:%llx, end:%llx\n", sec->begin, sec->end);
	// }
    ASSERT(p->pgdir.pt);
    p->ucontext->x[0] = 0;
    p->ucontext->elr = 0x400000;
    p->ucontext->spsr = 0;
    //p->cwd = inodes.get(ROOT_INODE_NO);
    set_parent_to_this(p);
    set_container_to_this(p);
    start_proc(p, trap_return, 0);
}

NO_RETURN void kernel_entry() {
    printk("hello world!\n");
    
    sd_init();
    //sd_test();
    do_rest_init();
    // proc_test();
    // user_proc_test();
    // container_test();
    // pgfault_first_test();
    // pgfault_second_test();

    // TODO: map init.S to user space and trap_return to run icode
    //printk("hello world!\n");
    root_creat_userproc();
    //printk("hello world!\n");
    while (1) { 
        yield();
        //printk("in root proc\n");
        if (panic_flag)
            break;
        arch_with_trap {
            arch_wfi();
        }
        //printk("cpu:%d\n", cpuid());
    }
    set_cpu_off();
    arch_stop_cpu();
}

NO_INLINE NO_RETURN void _panic(const char* file, int line) {
    printk("=====%s:%d PANIC%d!=====\n", file, line, cpuid());
    panic_flag = true;
    set_cpu_off();
    for (int i = 0; i < NCPU; i++) {
        if (cpus[i].online)
            i--;
    }
    printk("Kernel PANIC invoked at %s:%d. Stopped.\n", file, line);
    arch_stop_cpu();
}

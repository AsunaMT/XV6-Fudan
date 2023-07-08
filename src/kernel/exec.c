#include <elf.h>
#include <common/string.h>
#include <common/defines.h>
#include <kernel/console.h>
#include <kernel/proc.h>
#include <kernel/sched.h>
#include <kernel/syscall.h>
#include <kernel/pt.h>
#include <kernel/mem.h>
#include <kernel/paging.h>
#include <aarch64/trap.h>
#include <fs/file.h>
#include <fs/inode.h>
#include <kernel/printk.h>

//static u64 auxv[][2] = {{AT_PAGESZ, PAGE_SIZE}};
extern int fdalloc(struct file* f);

static const u64 MAXARG = 32;

static void check_elfmag(unsigned char*	e_ident);
static int load_seg(Inode *ip, struct pgdir *pd, u64 va, usize offset, usize size, u64 flags);
static int load_bss(struct pgdir *pd, u64 va, u64 size);

int execve(const char *path, char *const argv[], char *const envp[]){
    // TODO
	//这里没有管 envp
    //printk("in exec: %s, proc id:%d\n", path, thisproc()->pid);
    if(envp){
    }
    OpContext ctx;
    bcache.begin_op(&ctx);
    auto this_proc = thisproc();
    // free_sections(&this_proc->pgdir);
    // init_sections(&this_proc->pgdir.section_head);
	// Step1: Load data from the file stored in `path`.
    Inode *inode_ptr = namei(path, &ctx);
    if(!inode_ptr){
        bcache.end_op(&ctx);
        return -1;
    }
    inodes.lock(inode_ptr);

    Elf64_Ehdr elf_header;
    inodes.read(inode_ptr, (u8*)&elf_header, 0, sizeof(Elf64_Ehdr));
	check_elfmag(elf_header.e_ident);
    Elf64_Off e_phoff = elf_header.e_phoff;
    Elf64_Half e_phnum = elf_header.e_phnum;

    u64 argc = 0;
    u64 ustack[MAXARG + 2];
    // Step2: Load program headers and the program itself
    u64 offset = e_phoff;
    Elf64_Phdr prog_header;
    
	struct pgdir* new_pgdir = kalloc(sizeof(struct pgdir));
    ASSERT(new_pgdir);
	init_pgdir(new_pgdir);
    //printk("exec here1\n");
	int ok;
    for(int i = 0; i < e_phnum; i++) {
        if(inodes.read(inode_ptr, (u8*)&prog_header, offset, sizeof(Elf64_Phdr)) != sizeof(Elf64_Phdr)){
    		PANIC();
            goto error;
        }

        offset += sizeof(Elf64_Phdr);
        if(prog_header.p_type != PT_LOAD){
            continue;
		}
        if(prog_header.p_vaddr + prog_header.p_filesz < prog_header.p_vaddr){
    		PANIC();
            goto error;
        }
        // if(prog_header.p_vaddr % PAGE_SIZE != 0){
    	// 	PANIC();
        //     goto error;
        // }

        //printk("exec here2\n");
        if((prog_header.p_flags & PF_R) && (prog_header.p_flags & PF_X)){
        	// 可执行内容 -> 导入到text段
            ok = load_seg(inode_ptr, new_pgdir, prog_header.p_vaddr, 
				    prog_header.p_offset, prog_header.p_filesz, ST_TEXT);
            if(ok < 0){
    			PANIC();
                goto error;
			}
        } else if((prog_header.p_flags & PF_R) && (prog_header.p_flags & PF_W)) {
        	// data+bss -> 先导入data (ST_FILE)，再导入bss
            ok = load_seg(inode_ptr, new_pgdir, prog_header.p_vaddr,
				    prog_header.p_offset, prog_header.p_filesz, ST_FILE);
            if(ok < 0){
    			PANIC();
                goto error;
			}
            ok = load_bss(new_pgdir, prog_header.p_vaddr + prog_header.p_filesz,
				    prog_header.p_memsz - prog_header.p_filesz);
            if(ok < 0){
    			PANIC();
                goto error;
			}
        } else {
            PANIC();
		}
    }
    inodes.unlock(inode_ptr);
    inodes.put(&ctx, inode_ptr);
    bcache.end_op(&ctx);
    inode_ptr = NULL;

    //printk("exec here3\n");
    // Step3: Allocate and initialize user stack.
    //sp = STACK_TOP;
    u64 sp = STACK_BOTTOM - 64;
    alloc_stack_sec(new_pgdir);
    if(argv != NULL){
        for(; argv[argc]; argc++){
            if(argc >= MAXARG){
                PANIC();
                goto error;
            }
            sp -= strlen(argv[argc]) + 1;
            sp -= sp % 16; 
            if(sp < STACK_TOP){
                PANIC();
                goto error;
            }
            if(copyout(new_pgdir, (void*)sp, argv[argc], strlen(argv[argc]) + 1) < 0){
                PANIC();
                goto error;
            }
            ustack[argc + 1] = sp;
        }
    }
    ustack[0] = argc;
    ustack[argc + 1] = 0;
    sp -= (argc + 2) * sizeof(u64);
    sp -= sp % 16;
    //printk("exec here4\n");
    if(sp < STACK_TOP){
    	PANIC();
        goto error;
	}
    if(copyout(new_pgdir, (void*)sp, (void*)ustack, (argc + 2) * sizeof(u64)) < 0){
    	PANIC();
        goto error;
	}
   
    //printk("exec here5\n");
    this_proc->ucontext->x[1] = (argc == 0) ? 0 : (sp + sizeof(u64));
    this_proc->ucontext->x[2] = 0;
    this_proc->ucontext->elr = elf_header.e_entry; 
    this_proc->ucontext->sp = sp; 
    //printk("111\n");
	free_pgdir(&this_proc->pgdir);
    //printk("222\n");
	init_pgdir(&this_proc->pgdir);
    //printk("333\n");
	copy_pgdir(new_pgdir, &this_proc->pgdir);   
    //printk("444\n");
	//free_pgdir(new_pgdir);
    //printk("555\n");
	kfree(new_pgdir);   
    //attach_pgdir(&this_proc->pgdir);
	arch_fence();
    arch_tlbi_vmalle1is();
	arch_fence();
    //printk("exec here6\n");
    return argc; 

error:
    if(&thisproc()->pgdir){
        free_pgdir(&thisproc()->pgdir);
	}
    if(inode_ptr){
        inodes.unlock(inode_ptr);
        inodes.put(&ctx, inode_ptr);
        bcache.end_op(&ctx);
    }
    return -1;
}

static int load_seg(Inode *ip, struct pgdir *pd, u64 va, usize offset, usize size, u64 flags){
    // if((va % PAGE_SIZE) != 0){
    //     printk("loadseg_va_pg");
    //     return -1;
    // }
    if(!(flags & ST_FILE)) {
        return -1;
    }
    //struct section* sec = NULL;
    u64 end = PAGE_UP(va + size);
    auto sec = get_section_by_flag(pd, flags);
    ASSERT(sec && sec->begin == sec->end);

    u64 fgs = PTE_USER_DATA;

	fgs = (flags & ST_RO) ? (fgs | PTE_RO) : (fgs | PTE_RW);
    u64 begin = PAGE_BASE(va);
    //printk("seg begin = %llx, end = %llx\n", begin, end);
    for(u64 va_pg = begin; va_pg < end;){
        // ps: 取消了swap
        void* ka_page = alloc_page_for_user();
        ASSERT(ka_page);
        vmmap(pd, va_pg, ka_page, fgs);  
        u64 offset_offset, this_begin;
        if(va_pg < va){
            offset_offset = 0;
            this_begin = va;
        } else {
            offset_offset = va_pg - va;
            this_begin = va_pg;
        }
        u64 count;
        //看是否在该页结束
        va_pg += PAGE_SIZE; //here add pgsize
        if(va_pg > size + va){
            count = size + va - this_begin;
		} else {
            count = va_pg - this_begin;
		}
        if(inodes.read(ip, (u8*)ka_page + this_begin - va_pg + PAGE_SIZE,
            offset + offset_offset, count) != count){
            return -1;
		}
    }
	sec->flags = ST_FILE;
	if(flags & ST_RO){
		sec->flags |= ST_RO;
    }
    sec->begin = begin;
	sec->end = end;
    //printk("seg begin = %llx, end = %llx\n", begin, end);
    return 0;
}

static int load_bss(struct pgdir *pd, u64 va, u64 size){
    // if((va % PAGE_SIZE) != 0){
    //     printk("loadbss_va_pg");
    //     return -1;
    // }
    auto sec = get_section_by_flag(pd, ST_BSS);
    ASSERT(sec);
    sec->end = PAGE_UP(va + size);
    //printk("bss begin = %llx, end = %llx\n", PAGE_BASE(va), PAGE_UP(va + size));
    for(u64 va_pg = PAGE_BASE(va); va_pg < PAGE_UP(va + size); va_pg += PAGE_SIZE){
        if(va_pg >= va){
            void* ka_page = alloc_page_for_user();
            ASSERT(ka_page);
            vmmap(pd, va_pg, ka_page, PTE_USER_DATA);
            memset(ka_page, 0, PAGE_SIZE);
        } else {
			void* ka_page = (void*)P2K(PAGE_BASE(
                *get_pte(pd, va_pg, false)
                ));
            ASSERT(ka_page);
			memset((void*)(ka_page + va - va_pg), 0, PAGE_SIZE - (va - va_pg));
        }
    }
    return 0;
}

static void check_elfmag(unsigned char*	e_ident){
    ASSERT(e_ident[EI_MAG0] == ELFMAG0);
    ASSERT(e_ident[EI_MAG1] == ELFMAG1);
    ASSERT(e_ident[EI_MAG2] == ELFMAG2);
    ASSERT(e_ident[EI_MAG3] == ELFMAG3);
}
 

/* 
 

*步骤1:从存储在' path '中的文件中加载数据。  

*第一个' sizeof(struct Elf64_Ehdr) '字节是ELF头部分。  

*你应该检查ELF魔术数字，并获得' e_phoff '和' e_phnum '，这是程序头的开始字节。  

＊  

*步骤2:加载程序头和程序本身  

*程序头存储方式为:struct Elf64_Phdr phdr[e_phnum];  

* e_phoff是文件头文件的偏移量，即phdr[0]的地址。  

*对于每个程序头，如果类型(p_type)是LOAD，你应该加载它们:  

一种天真的方法是  

*(1)分配内存，va区域[vaddr, vaddr+filesz)  

*(2)复制[offset, offset +filesz)的文件到va [vaddr, vaddr+filesz)的内存  

客观的方法是  

*由于我们应用了动态虚拟内存管理，你可以尝试只设置文件和偏移量(惰性分配)  

*(提示:在这个实验室的大多数可执行文件中有两个可加载的程序头，第一个头表示文本部分(flag=RX)，
第二个头是data+bss部分(flag=RW)。 您可以通过检查标题标志来验证这一点。   

第二个头有[p_vaddr, p_vaddr+p_filesz)数据部分和[p_vaddr+p_filesz, p_vaddr+p_memsz) BSS部分，需要设置为0，
您可能必须将数据和BSS放在一个struct部分中。 鼓励使用零页的COW)  

*步骤3:分配和初始化用户堆栈。  

*用户堆栈的va不需要是任何固定的值。 它可以是随机的。 (提示:你可以一次直接分配用户栈，或者应用惰性分配)  

*推参数字符串 
*/
#include <kernel/syscall.h>
#include <kernel/sched.h>
#include <kernel/printk.h>
#include <common/sem.h>
#include <kernel/pt.h>
#include <kernel/paging.h>

void* syscall_table[NR_SYSCALL];

void syscall_entry(UserContext* context)
{
    // TODO
    // Invoke syscall_table[id] with args and set the return value.
    // id is stored in x8. args are stored in x0-x5. return value is stored in x0.
    u64 id = context->x[8];
    if (id < NR_SYSCALL) {
        if(syscall_table[id] == NULL) PANIC();
        // if(id != 459)
        //     printk("syscall id:%lld\n", id);
        context->x[0] = 
            (*(u64(*)(u64, u64, u64, u64, u64, u64))syscall_table[id])(
            context->x[0], context->x[1], context->x[2], context->x[3], context->x[4], context->x[5]
            );
    } else PANIC();
}

#define has_flag(val, flag) ((u64)(val) & (u64)(flag))
#define no_flag(val, flag) (!((u64)(val) & (u64)(flag)))
// check if the virtual address [start,start+size) is READABLE by the current user process
bool user_readable(const void* start, usize size) {
    // TODO
    //by pte
    u64 va = (u64)start;
    while (size > 0) {
        u64 va_pg = PAGE_BASE(va);
        auto pte = get_pte(&thisproc()->pgdir, va_pg, false);
        if(pte == NULL) {
            printk("r false1\n");
            return false;
        }
        if(no_flag(*pte, PTE_USER_DATA)) {
            printk("r false2\n");
            return false;
        }
        u64 n = PAGE_SIZE - va + va_pg;
        if(n > size) {
            n = size;
        }
        va = va_pg + PAGE_SIZE;
        size -= n;
    }
    return true;
    //by section
    // for(u64 va = PAGE_BASE((u64)start); va < PAGE_UP((u64)start + size); va += PAGE_SIZE){
    //     struct section *sec = NULL;
    //     struct pgdir *pd = &thisproc()->pgdir;
    //     _for_in_list(node, &pd->section_head){
    //         if(node == &pd->section_head)  continue;
    //         sec = section_of(node);
    //         if(sec->begin <= va && sec->end > va){
    //             va = sec->end;
    //             continue;
    //         }
    //     }
    //     return false;
    // }
    // return true;
}

// check if the virtual address [start,start+size) is READABLE & WRITEABLE by the current user process
bool user_writeable(const void* start, usize size) {
    // TODO
    //by pte
    u64 va = (u64)start;
    while (size > 0) {
        u64 va_pg = PAGE_BASE(va);
        auto pte = get_pte(&thisproc()->pgdir, va_pg, false);
        if(pte == NULL) {
            printk("w false1\n");
            return false;
        }
        if(no_flag(*pte, PTE_USER_DATA) || has_flag(*pte, PTE_RO)) {
            printk("w false2\n");
            return false;
        }
        u64 n = PAGE_SIZE - va + va_pg;
        if(n > size) {
            n = size;
        }
        va = va_pg + PAGE_SIZE;
        size -= n;
    }
    return true;
    //by section
    // for(u64 va = PAGE_BASE((u64)start); va < PAGE_UP((u64)start + size); va += PAGE_SIZE){
    //     struct section *sec = NULL;
    //     struct pgdir *pd = &thisproc()->pgdir;
    //     _for_in_list(node, &pd->section_head){
    //         if(node == &pd->section_head)  continue;
    //         sec = section_of(node);
    //         if(sec->begin <= va && sec->end > va 
    //             && no_flag(sec->flags, ST_RO)){
    //             va = sec->end;
    //             continue;
    //         }
    //     }
    //     return false;
    // }
    // return true;
}

// get the length of a string including tailing '\0' in the memory space of current user process
// return 0 if the length exceeds maxlen or the string is not readable by the current user process
usize user_strlen(const char* str, usize maxlen) {
    for (usize i = 0; i < maxlen; i++) {
        if (user_readable(&str[i], 1)) {
            if (str[i] == 0)
                return i + 1;
        } else
            return 0;
    }
    return 0;
}

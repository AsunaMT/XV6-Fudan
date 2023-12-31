//
// File-system system calls.
// Mostly argument checking, since we don't trust
// user code, and calls into file.c and fs.c.
//

#include <fcntl.h>

#include <aarch64/mmu.h>
#include <common/defines.h>
#include <common/spinlock.h>
#include <kernel/printk.h>
#include <kernel/proc.h>
#include <kernel/sched.h>
#include <kernel/printk.h>
#include <kernel/mem.h>
#include <kernel/paging.h>
#include <fs/file.h>
#include <fs/fs.h>
#include <sys/syscall.h>
#include <kernel/mem.h>
#include "syscall.h"
#include <fs/pipe.h>
#include <common/string.h>
#include <fs/inode.h>
#include <fs/file.h>

struct iovec {
    void* iov_base; /* Starting address. */
    usize iov_len; /* Number of bytes to transfer. */
};


// get the file object by fd
// return null if the fd is invalid
static struct file* fd2file(int fd) {
    // TODO
    if (fd >= 0 && fd < NOFILE) {
        return thisproc()->oftable.ofd[fd];
    }
    return NULL;
}
/*
 * Allocate a file descriptor for the given file.
 * Takes over file reference from caller on success.
 */
int fdalloc(struct file* f) {
    /* TODO: Lab10 Shell */
    auto this_proc = thisproc();
    for(int i = 0; i < NOFILE; i++){
        if(this_proc->oftable.ofd[i] == NULL){
            this_proc->oftable.ofd[i] = f;
            return i;
        }
    }
    return -1;
}

int fdfree(int fd){
    if(fd < 0 || fd > NOFILE - 1) return -1;
    thisproc()->oftable.ofd[fd] = NULL;
    return 0;
}

define_syscall(ioctl, int fd, u64 request) {
    ASSERT(request == 0x5413);
    (void)fd;
    return 0;
}

/*
 *	map addr to a file
 */
// define_syscall(mmap, void* addr, int length, int prot, int flags, int fd, int offset) {
//     // TODO
// }

// define_syscall(munmap, void *addr, size_t length) {
//     // TODO
// }

/*
 * Get the parameters and call filedup.
 */
define_syscall(dup, int fd) {
    struct file* f = fd2file(fd);
    if (!f)
        return -1;
    fd = fdalloc(f);
    if (fd < 0)
        return -1;
    filedup(f);
    return fd;
}

/*
 * Get the parameters and call fileread.
 */
define_syscall(read, int fd, char* buffer, int size) {
    struct file* f = fd2file(fd);
    if (!f || size <= 0 || !user_writeable(buffer, size))
        return -1;
    return fileread(f, buffer, size);
}

/*
 * Get the parameters and call filewrite.
 */
define_syscall(write, int fd, char* buffer, int size) {
    struct file* f = fd2file(fd);
    if (!f || size <= 0 || !user_readable(buffer, size))
        return -1;
    return filewrite(f, buffer, size);
}

define_syscall(writev, int fd, struct iovec *iov, int iovcnt) {
    struct file* f = fd2file(fd);
    struct iovec *p;
    if (!f || iovcnt <= 0 || !user_readable(iov, sizeof(struct iovec) * iovcnt))
        return -1;
    usize tot = 0;
    for (p = iov; p < iov + iovcnt; p++) {
        if (!user_readable(p->iov_base, p->iov_len))
            return -1;
        tot += filewrite(f, p->iov_base, p->iov_len);
    }
    return tot;
}

/*
 * Get the parameters and call fileclose.
 * Clear this fd of this process.
 */
define_syscall(close, int fd) {
    /* TODO: Lab10 Shell */
    auto f = fd2file(fd);
    if(!f){
        return -1;
    }
    thisproc()->oftable.ofd[fd] = NULL;
    fileclose(f);
    return 0;
}

/*
 * Get the parameters and call filestat.
 */
define_syscall(fstat, int fd, struct stat* st) {
    struct file* f = fd2file(fd);
    if (!f || !user_writeable(st, sizeof(*st)))
        return -1;
    return filestat(f, st);
}

define_syscall(newfstatat, int dirfd, const char* path, struct stat* st, int flags) {
    if (!user_strlen(path, 256) || !user_writeable(st, sizeof(*st)))
        return -1;
    if (dirfd != AT_FDCWD) {
        printk("sys_fstatat: dirfd unimplemented\n");
        return -1;
    }
    if (flags != 0) {
        printk("sys_fstatat: flags unimplemented\n");
        return -1;
    }

    Inode* ip;
    OpContext ctx;
    bcache.begin_op(&ctx);
    if ((ip = namei(path, &ctx)) == 0) {
        bcache.end_op(&ctx);
        return -1;
    }
    inodes.lock(ip);
    stati(ip, st);
    inodes.unlock(ip);
    inodes.put(&ctx, ip);
    bcache.end_op(&ctx);

    return 0;
}

// Is the directory dp empty except for "." and ".." ?
static int isdirempty(Inode* dp) {
    usize off;
    DirEntry de;

    for (off = 2 * sizeof(de); off < dp->entry.num_bytes; off += sizeof(de)) {
        if (inodes.read(dp, (u8*)&de, off, sizeof(de)) != sizeof(de))
            PANIC();
        if (de.inode_no != 0)
            return 0;
    }
    return 1;
}

define_syscall(unlinkat, int fd, const char* path, int flag) {
    ASSERT(fd == AT_FDCWD && flag == 0);
    Inode *ip, *dp;
    DirEntry de;
    char name[FILE_NAME_MAX_LENGTH];
    usize off;
    if (!user_strlen(path, 256))
        return -1;
    OpContext ctx;
    bcache.begin_op(&ctx);
    if ((dp = nameiparent(path, name, &ctx)) == 0) {
        bcache.end_op(&ctx);
        return -1;
    }

    inodes.lock(dp);

    // Cannot unlink "." or "..".
    if (strncmp(name, ".", FILE_NAME_MAX_LENGTH) == 0
        || strncmp(name, "..", FILE_NAME_MAX_LENGTH) == 0)
        goto bad;

    usize inumber = inodes.lookup(dp, name, &off);
    if (inumber == 0)
        goto bad;
    ip = inodes.get(inumber);
    inodes.lock(ip);

    if (ip->entry.num_links < 1)
        PANIC();
    if (ip->entry.type == INODE_DIRECTORY && !isdirempty(ip)) {
        inodes.unlock(ip);
        inodes.put(&ctx, ip);
        goto bad;
    }

    memset(&de, 0, sizeof(de));
    if (inodes.write(&ctx, dp, (u8*)&de, off, sizeof(de)) != sizeof(de))
        PANIC();
    if (ip->entry.type == INODE_DIRECTORY) {
        dp->entry.num_links--;
        inodes.sync(&ctx, dp, true);
    }
    inodes.unlock(dp);
    inodes.put(&ctx, dp);
    ip->entry.num_links--;
    inodes.sync(&ctx, ip, true);
    inodes.unlock(ip);
    inodes.put(&ctx, ip);
    bcache.end_op(&ctx);
    return 0;

bad:
    inodes.unlock(dp);
    inodes.put(&ctx, dp);
    bcache.end_op(&ctx);
    return -1;
}
/*
 * Create an inode.
 *
 * Example:
 * Path is "/foo/bar/bar1", type is normal file.
 * You should get the inode of "/foo/bar", and
 * create an inode named "bar1" in this directory.
 *
 * If type is directory, you should additionally handle "." and "..".
 */
Inode* create(const char* path, short type, short major, short minor, OpContext* ctx) {
    /* TODO: Lab10 Shell */
    Inode* dir_p;
    char name[FILE_NAME_MAX_LENGTH];

    if((dir_p = nameiparent(path, name, ctx)) == 0){
        return 0;
    }
    inodes.lock(dir_p);
    Inode* ip;
    auto ino = inodes.lookup(dir_p, name, 0);
    //if(ino <= 0) printk("ino:%llx\n", ino);
    if(ino > 0){
        ip = inodes.get(ino);
        ASSERT(ip);
        inodes.unlock(dir_p);
        inodes.put(ctx, dir_p);
        inodes.lock(ip);
        if(type == INODE_REGULAR && ip->entry.type == INODE_REGULAR){
            inodes.unlock(ip);
            return ip;
        }
        if (type == INODE_DIRECTORY && ip->entry.type == INODE_DEVICE){
            inodes.unlock(ip);
            return ip;
        }
        inodes.unlock(ip);
        inodes.put(ctx, ip);
        return NULL;
    }

    if((ip = inodes.get(inodes.alloc(ctx, type))) == 0) PANIC();

    inodes.lock(ip);
    ip->entry.major = major;
    ip->entry.minor = minor;
    ip->entry.num_links = 1;
    inodes.sync(ctx, ip, true);

    if(type == INODE_DIRECTORY){  // Create . and .. entries.
        dir_p->entry.num_links++;  
        inodes.sync(ctx, dir_p, true);
        inodes.insert(ctx, ip, ".", ip->inode_no);
        inodes.insert(ctx, ip, "..", dir_p->inode_no);
    }

    inodes.insert(ctx, dir_p, name, ip->inode_no);
    inodes.unlock(dir_p);
    inodes.put(ctx, dir_p);
    return ip;
}

define_syscall(openat, int dirfd, const char* path, int omode) {
    int fd;
    struct file* f;
    Inode* ip;
    //printk("in openat\n");

    if (!user_strlen(path, 256))
        return -1;

    if (dirfd != AT_FDCWD) {
        printk("sys_openat: dirfd unimplemented\n");
        return -1;
    }

    OpContext ctx;
    bcache.begin_op(&ctx);
    if (omode & O_CREAT) {
        // FIXME: Support acl mode.
        ip = create(path, INODE_REGULAR, 0, 0, &ctx);
        if (ip == 0) {
            bcache.end_op(&ctx);
            return -1;
        }
    } else {
        if ((ip = namei(path, &ctx)) == 0) {
            bcache.end_op(&ctx);
            return -1;
        }
        inodes.lock(ip);
    }

    if ((f = filealloc()) == 0 || (fd = fdalloc(f)) < 0) {
        if (f)
            fileclose(f);
        inodes.unlock(ip);
        inodes.put(&ctx, ip);
        bcache.end_op(&ctx);
        return -1;
    }
    inodes.unlock(ip);
    bcache.end_op(&ctx);

    f->type = FD_INODE;
    f->ip = ip;
    f->off = 0;
    f->readable = !(omode & O_WRONLY);
    f->writable = (omode & O_WRONLY) || (omode & O_RDWR);
    return fd;
}

define_syscall(mkdirat, int dirfd, const char* path, int mode) {
    Inode* ip;
    if (!user_strlen(path, 256))
        return -1;
    if (dirfd != AT_FDCWD) {
        printk("sys_mkdirat: dirfd unimplemented\n");
        return -1;
    }
    if (mode != 0) {
        printk("sys_mkdirat: mode unimplemented\n");
        return -1;
    }
    OpContext ctx;
    bcache.begin_op(&ctx);
    if ((ip = create(path, INODE_DIRECTORY, 0, 0, &ctx)) == 0) {
        bcache.end_op(&ctx);
        return -1;
    }
    inodes.unlock(ip);
    inodes.put(&ctx, ip);
    bcache.end_op(&ctx);
    return 0;
}

define_syscall(mknodat, int dirfd, const char* path, int major, int minor) {
    Inode* ip;
    if (!user_strlen(path, 256))
        return -1;
    if (dirfd != AT_FDCWD) {
        printk("sys_mknodat: dirfd unimplemented\n");
        return -1;
    }
    printk("mknodat: path '%s', major:minor %d:%d\n", path, major, minor);
    OpContext ctx;
    bcache.begin_op(&ctx);
    if ((ip = create(path, INODE_DEVICE, major, minor, &ctx)) == 0) {
        bcache.end_op(&ctx);
        return -1;
    }
    inodes.unlock(ip);
    inodes.put(&ctx, ip);
    bcache.end_op(&ctx);
    return 0;
}

define_syscall(chdir, const char* path) {
    // TODO
    // change the cwd (current working dictionary) of current process to 'path'
    // you may need to do some validations
    Inode* ip;
    auto this_proc = thisproc();
    OpContext ctx;
    bcache.begin_op(&ctx);
    if((ip = namei(path, &ctx)) == NULL){
        bcache.end_op(&ctx);
        return -1;
    }
    inodes.lock(ip);
    if(ip->entry.type != INODE_DIRECTORY){
        inodes.unlock(ip);
        inodes.put(&ctx, ip);
        bcache.end_op(&ctx);
        return -1;
    }
    inodes.unlock(ip);
    inodes.put(&ctx, this_proc->cwd);
    this_proc->cwd = ip;
    bcache.end_op(&ctx);
    return 0;
}

define_syscall(pipe2, int *fd, int flags) {
    // TODO
    File* read_f;
    File* write_f;
    if(pipeAlloc(&read_f, &write_f)){
        return -1;
    }
    // fd[0] = (u64)(&read_f);
    // fd[1] = (u64)(&write_f);
    // printk("%d\n", flags);
    // return 0;
    int fd_f0, fd_f1;
    bool fd_error;
    if((fd_f0 = fdalloc(read_f)) < 0) {
        fd_error = true;
    }
    if((fd_f1 = fdalloc(write_f)) < 0){
        if(fd_f0 >= 0){
            fdfree(fd_f0);
        }
        fd_error = true;
    }
    if(fd_error){
        fileclose(read_f);
        fileclose(write_f);
        return -1;
    }
    fd[0] = fd_f0;
    fd[1] = fd_f0;
    printk("%d\n", flags);
    return 0;
}

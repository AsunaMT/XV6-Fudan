#include<kernel/console.h>
#include<kernel/init.h>
#include<aarch64/intrinsic.h>
#include<kernel/sched.h>
#include<driver/uart.h>
#include<driver/interrupt.h>
#include<kernel/printk.h>
#define INPUT_BUF 128
struct {
    char buf[INPUT_BUF];
    usize r;  // Read index
    usize w;  // Write index
    usize e;  // Edit index
} input;
#define C(x)      ((x) - '@')  // Control-x
// #define BACKSPACE '\x7f'
#define BACKSPACE_SIGN 0x100
//#define BACKSPACE_SIGN (int)(BIT(sizeof(char) + 1))

SpinLock consolelock;
Semaphore readsem;

static void console_putchar(int c);

void console_intr_normal(){
    console_intr(uart_get_char);
}

define_init(console_intr){
    set_interrupt_handler(IRQ_AUX, console_intr_normal);
}

define_early_init(input){
    init_spinlock(&consolelock);
    init_sem(&readsem, 0);
    input.r = input.w = input.e = 0;
}

isize console_write(Inode *ip, char *buf, isize n) {
    // TODO
    inodes.unlock(ip);
    _acquire_spinlock(&consolelock);
    for(isize i = 0; i < n; i++){
        console_putchar(buf[i]);
    }
    _release_spinlock(&consolelock);
    inodes.lock(ip);

    return n;
}

isize console_read(Inode *ip, char *dst, isize n) {
    // TODO
    inodes.unlock(ip);
    isize num = n;
    char c;
    _acquire_spinlock(&consolelock);
    while(n > 0){
        while(input.r == input.w){
            if(thisproc()->killed){
                _release_spinlock(&consolelock);
                inodes.lock(ip);
                return -1;
            }
            _lock_sem(&readsem);
            _release_spinlock(&consolelock);
            ASSERT(_wait_sem(&readsem, false));
            _acquire_spinlock(&consolelock);
        }
        c = input.buf[input.r++ % INPUT_BUF];
        if(c == C('D')){  // EOF
            if(n < num){
                input.r--;
            }
            break;
        }
        *(dst++) = c;
        n--;
        if(c == '\n'){
            break;
        }
    }
    _release_spinlock(&consolelock);
    inodes.lock(ip);
    return num - n;
}

void console_intr(char (*getc)()) {
    // TODO
    char in;
    _acquire_spinlock(&consolelock);
    if((in = getc()) > '\0'){
        switch(in){
            case '\b': case '\x7f': // backspace(0x08) and delete(0x7f)
                if(input.e > input.w){
                    input.e--;
                    console_putchar(BACKSPACE_SIGN);
                }
                break;
            case C('U'):  
                while(input.e > input.w && input.buf[(input.e - 1) % INPUT_BUF] != '\n'){
                    input.e--;
                    console_putchar(BACKSPACE_SIGN);
                }
                break;
            case C('C'):
                //功能不完善，禁用
                PANIC();
                ASSERT(kill(thisproc()->pid) < 1); 
                break;
            case '\n': case C('D'):
                if(in != 0 && input.e - input.r < INPUT_BUF){
                    uart_put_char('\n');
                    input.buf[input.e++ % INPUT_BUF] = '\n';
                    input.w = input.e;
                    post_all_sem(&readsem);
                }
                break;
            // case C("D"):
            //     if(in != 0 && input.e-input.r < INPUT_BUF){
            //         uart_put_char('\n');
            //         input.buf[input.e++ % INPUT_BUF] = -1;   //EOF
            //         input.w = input.e;
            //     }
            //     break;
            default:
                if(in != 0 && input.e - input.r < INPUT_BUF){
                    in = (in == '\r') ? '\n' : in;
                    input.buf[input.e++ % INPUT_BUF] = in;
                    console_putchar(in);
                    if(input.e - input.r == INPUT_BUF || in == '\n'){
                        input.w = input.e;
                        post_sem(&readsem);
                    }
                }
                break;
        }
    }
    _release_spinlock(&consolelock);
}


static void console_putchar(int c){
  if(c == BACKSPACE_SIGN){
    uart_put_char('\b'); 
    uart_put_char(' '); 
    uart_put_char('\b');
  } else {
    uart_put_char(c);
  }
}
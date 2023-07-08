#include <common/bit_op.h>
#include <kernel/printk.h>

int bitmap_fetch0(bool* bitmap, unsigned int size)
{   
    //printk("\nbitmap:%xmax:%d", bitmap, max);
    for(unsigned int i = 0; i < size; i++){
        if(bitmap[i] == 0){
            bitmap[i] = 1;
            return i;
        } 
    };
    return -1;
}

void bitmap_set(bool* bitmap, unsigned int index)
{   
    //printk("\n%dsetin-%x--", index, *bitmap);
    //*bitmap |= (1 << index);
    //printk("%x-setout\n", *bitmap);
    bitmap[index] = 1;
}

void bitmap_reset(bool* bitmap, unsigned int index)
{
    //printk("\n%dresetin-%x--", index, *bitmap);
    //*bitmap &= (~(1 << index));
    //printk("%x-resetout\n", *bitmap);
    bitmap[index] = 0;
}
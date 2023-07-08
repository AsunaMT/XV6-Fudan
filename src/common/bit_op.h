#pragma once

#include <common/defines.h>

int bitmap_fetch0(bool* bitmap, unsigned int size);

void bitmap_set(bool* bitmap, unsigned int index);

void bitmap_reset(bool* bitmap, unsigned int index);
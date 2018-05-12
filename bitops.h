#pragma once
#include "compat.h"

#ifndef __BITOPS_H__
#define __BITOPS_H__

#ifndef __SIZEOF_LONG__
#define __SIZEOF_LONG__ sizeof(long)
#endif

#ifndef __WORDSIZE
#define __WORDSIZE (__SIZEOF_LONG__ * 8)
#endif

#ifndef BITS_PER_LONG
# define BITS_PER_LONG __WORDSIZE
#endif
#define BITS_PER_BYTE		8

#define DIV_ROUND_UP(n, d)	(((n) + (d) - 1) / (d))
#define BITS_TO_LONGS(nr)	DIV_ROUND_UP(nr, BITS_PER_BYTE * sizeof(long))

#define BIT_WORD(nr)		((nr) / BITS_PER_LONG)

#define BITMAP_FIRST_WORD_MASK(start) (~0UL << ((start) & (BITS_PER_LONG - 1)))
#define BITMAP_LAST_WORD_MASK(nbits) (~0UL >> (-(nbits) & (BITS_PER_LONG - 1)))

#define __round_mask_ul(x, y) ((unsigned long) ((y)-1))
#define round_down_ul(x, y) ((x) & ~__round_mask_ul(x, y))

#define BIT(nr)			(1UL << (nr))
#define BIT_MASK(nr)		(1UL << ((nr) % BITS_PER_LONG))
#define BIT_WORD(nr)		((nr) / BITS_PER_LONG)
#define BITS_TO_TYPE(nr, t)	(((nr)+(t)-1)/(t))

void bitmap_set(unsigned long *map, unsigned int start, int len);
int bitmap_weight(const unsigned long *bitmap, unsigned int bits);
unsigned long find_first_bit(const unsigned long *addr, unsigned long size);
unsigned long find_next_bit(const unsigned long *addr, unsigned long size, unsigned long offset);

#endif

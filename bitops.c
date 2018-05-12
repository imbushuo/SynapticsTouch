/* BitOps Linux Port */
#include <wdm.h>
#include <wdf.h>
#include "bitops.h"
#include "hweight.h"

void bitmap_set(unsigned long *map, unsigned int start, int len)
{
	unsigned long *p = map + BIT_WORD(start);
	const unsigned int size = start + len;
	int bits_to_set = BITS_PER_LONG - (start % BITS_PER_LONG);
	unsigned long mask_to_set = BITMAP_FIRST_WORD_MASK(start);

	while (len - bits_to_set >= 0)
	{
		*p |= mask_to_set;
		len -= bits_to_set;
		bits_to_set = BITS_PER_LONG;
		mask_to_set = ~0UL;
		p++;
	}

	if (len)
	{
		mask_to_set &= BITMAP_LAST_WORD_MASK(size);
		*p |= mask_to_set;
	}
}

int bitmap_weight(const unsigned long *bitmap, unsigned int bits)
{
	unsigned int k, lim = bits / BITS_PER_LONG;
	int w = 0;

	for (k = 0; k < lim; k++)
		w += hweight_long(bitmap[k]);

	if (bits % BITS_PER_LONG)
		w += hweight_long(bitmap[k] & BITMAP_LAST_WORD_MASK(bits));

	return w;
}

static inline unsigned long __ffs(unsigned long word)
{
	int num = 0;

#if defined(ARM64) || defined(AMD64)
	if ((word & 0xffffffff) == 0) {
		num += 32;
		word >>= 32;
	}
#endif
	if ((word & 0xffff) == 0) {
		num += 16;
		word >>= 16;
	}
	if ((word & 0xff) == 0) {
		num += 8;
		word >>= 8;
	}
	if ((word & 0xf) == 0) {
		num += 4;
		word >>= 4;
	}
	if ((word & 0x3) == 0) {
		num += 2;
		word >>= 2;
	}
	if ((word & 0x1) == 0)
		num += 1;
	return num;
}

unsigned long find_first_bit(const unsigned long *addr, unsigned long size)
{
	unsigned long idx;

	for (idx = 0; idx * BITS_PER_LONG < size; idx++) {
		if (addr[idx])
			return min(idx * BITS_PER_LONG + __ffs(addr[idx]), size);
	}

	return size;
}

/*
* This is a common helper function for find_next_bit, find_next_zero_bit, and
* find_next_and_bit. The differences are:
*  - The "invert" argument, which is XORed with each fetched word before
*    searching it for one bits.
*  - The optional "addr2", which is anded with "addr1" if present.
*/
static inline unsigned long _find_next_bit(const unsigned long *addr1,
	const unsigned long *addr2, unsigned long nbits,
	unsigned long start, unsigned long invert)
{
	unsigned long tmp;

	if (start >= nbits) return nbits;

	tmp = addr1[start / BITS_PER_LONG];
	if (addr2)
		tmp &= addr2[start / BITS_PER_LONG];
	tmp ^= invert;

	/* Handle 1st word. */
	tmp &= BITMAP_FIRST_WORD_MASK(start);
	start = round_down_ul(start, BITS_PER_LONG);

	while (!tmp) {
		start += BITS_PER_LONG;
		if (start >= nbits)
			return nbits;

		tmp = addr1[start / BITS_PER_LONG];
		if (addr2)
			tmp &= addr2[start / BITS_PER_LONG];
		tmp ^= invert;
	}

	return min(start + __ffs(tmp), nbits);
}

unsigned long find_next_bit(const unsigned long *addr, unsigned long size,
	unsigned long offset)
{
	return _find_next_bit(addr, NULL, size, offset, 0UL);
}

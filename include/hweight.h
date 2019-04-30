#pragma once
#ifndef __HEWEIGHT_H__
#define __HEWEIGHT_H__

unsigned int hweight32(unsigned int w);
ULONGLONG hweight64(ULONGLONG w);

static inline unsigned long hweight_long(unsigned long w)
{
	return sizeof(w) == 4 ? hweight32(w) : hweight64(w);
}

#endif
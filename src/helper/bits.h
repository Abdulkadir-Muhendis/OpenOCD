/*
 * Copyright (C) 2018, STMicroelectronics - All Rights Reserved
 * Author(s): Antonio Borneo <borneo.antonio@gmail.com> for STMicroelectronics
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * The content of this file is mainly copied/inspired from Linux kernel
 * code in include/linux/types.h include/linux/bitmap.h include/linux/bitops.h
 */

#ifndef OPENOCD_HELPER_BITS_H
#define OPENOCD_HELPER_BITS_H

#include <helper/types.h>

#define BIT(nr)                     (1UL << (nr))
#define BITS_PER_BYTE               8
#define BITS_PER_LONG               (BITS_PER_BYTE * sizeof(long))
#define BITS_TO_LONGS(nr)           DIV_ROUND_UP(nr, BITS_PER_BYTE * sizeof(long))
#define BIT_MASK(nr)                (1UL << ((nr) % BITS_PER_LONG))
#define BIT_WORD(nr)                ((nr) / BITS_PER_LONG)
#define DECLARE_BITMAP(name, bits)  unsigned long name[BITS_TO_LONGS(bits)]

/**
 * bitmap_zero - Clears all the bits in memory
 * @dst: the address of the bitmap
 * @nbits: the number of bits to clear
 */
static inline void bitmap_zero(unsigned long *dst, unsigned int nbits)
{
	if (nbits <= BITS_PER_LONG)
		*dst = 0UL;
	else {
		unsigned int len = BITS_TO_LONGS(nbits) * sizeof(unsigned long);
		memset(dst, 0, len);
	}
}

/**
 * clear_bit - Clear a bit in memory
 * @nr: the bit to set
 * @addr: the address to start counting from
 */
static inline void clear_bit(int nr, volatile unsigned long *addr)
{
	unsigned long mask = BIT_MASK(nr);
	unsigned long *p = ((unsigned long *)addr) + BIT_WORD(nr);

	*p &= ~mask;
}

/**
 * set_bit - Set a bit in memory
 * @nr: the bit to set
 * @addr: the address to start counting from
 */
static inline void set_bit(int nr, volatile unsigned long *addr)
{
	unsigned long mask = BIT_MASK(nr);
	unsigned long *p = ((unsigned long *)addr) + BIT_WORD(nr);

	*p  |= mask;
}

/**
 * test_bit - Determine whether a bit is set
 * @nr: bit number to test
 * @addr: Address to start counting from
 */
static inline int test_bit(int nr, const volatile unsigned long *addr)
{
	return 1UL & (addr[BIT_WORD(nr)] >> (nr & (BITS_PER_LONG-1)));
}

#endif /* OPENOCD_HELPER_BITS_H */

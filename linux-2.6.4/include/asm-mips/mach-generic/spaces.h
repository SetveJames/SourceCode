/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1996, 99 Ralf Baechle
 * Copyright (C) 2000, 2002  Maciej W. Rozycki
 * Copyright (C) 1990, 1999 by Silicon Graphics, Inc.
 */
#ifndef _ASM_MACH_SPACES_H
#define _ASM_MACH_SPACES_H

#include <linux/config.h>

#ifdef CONFIG_MIPS32

#define CAC_BASE		0x80000000
#define IO_BASE			0xa0000000
#define UNCAC_BASE		0xa0000000
#define MAP_BASE		0xc0000000

#endif /* CONFIG_MIPS32 */

#ifdef CONFIG_MIPS64

#ifdef CONFIG_DMA_NONCOHERENT
#define CAC_BASE		0x9800000000000000
#else
#define CAC_BASE		0xa800000000000000
#endif
#define IO_BASE			0x9000000000000000
#define UNCAC_BASE		0x9000000000000000
#define MAP_BASE		0xc000000000000000

#define TO_PHYS(x)		(             ((x) & TO_PHYS_MASK))
#define TO_CAC(x)		(CAC_BASE   | ((x) & TO_PHYS_MASK))
#define TO_UNCAC(x)		(UNCAC_BASE | ((x) & TO_PHYS_MASK))

#endif /* CONFIG_MIPS64 */

#endif /* _ASM_MACH_SPACES_H */

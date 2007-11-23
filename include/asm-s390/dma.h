/*
 *  include/asm-s390/delay.h
 *
 *  S390 version
 *
 *  This file exists so that an #include <dma.h> doesn't break anything.
 *
 *  -AC- we now rely on the lack of the maximum DMA channel definition to
 * cleanly support platforms with no DMA.
 */

#ifndef _ASM_DMA_H
#define _ASM_DMA_H

#include <asm/io.h>		/* need byte IO */

#endif /* _ASM_DMA_H */

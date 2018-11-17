#ifndef GBA_H
#define GBA_H

#include "uint.h"

extern void *fake_dma_page;
extern volatile u16 *videoBuffer;

#include "dma.h"

// Double volatile is pretty critical here
#define DMA ((volatile DMA_CONTROLLER *volatile) fake_dma_page)
#define UNUSED(param) ((void)((param)))

#endif

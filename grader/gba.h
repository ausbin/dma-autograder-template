#ifndef GBA_H
#define GBA_H

#include "uint.h"

extern volatile u16 *videoBuffer;

#include "dma.h"

extern DMA_CONTROLLER *_fake_dma(void);

#define DMA (_fake_dma())
#define UNUSED(param) ((void)((param)))

#endif

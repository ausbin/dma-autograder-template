#ifndef DMA_REGIONS_H
#define DMA_REGIONS_H

#include "dma_framework.h"

typedef struct {
    unsigned int original_index;
    unsigned int src_start;
    unsigned int src_end;
    unsigned int dst_start;
    unsigned int dst_end;
} dma_region_t;

extern void dma_regions_normalize(dma_region_t *, int);
extern void dma_regions_from_transfers(int, dma_transfer_t *, dma_region_t **, int *);
extern void dma_regions_assert(dma_region_t *, int, dma_region_t *, int);

#endif

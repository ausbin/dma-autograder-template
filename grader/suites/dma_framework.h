#ifndef DMA_FRAMEWORK_H
#define DMA_FRAMEWORK_H

#include "../uint.h"
#include "../dma.h"

enum state {
	DMA_STATE_SRC = 1 << 0,
	DMA_STATE_DST = 1 << 1,
	DMA_STATE_CNT = 1 << 2,
	DMA_STATE_ON  = 1 << 3
};

enum sim_chan {
	DMA_SIM_CHAN_0 = 1 << 0,
	DMA_SIM_CHAN_1 = 1 << 1,
	DMA_SIM_CHAN_2 = 1 << 2,
	DMA_SIM_CHAN_3 = 1 << 3
};

typedef struct dma_transfer {
    DMA_CONTROLLER flags;
    int state;
    struct dma_transfer *next;
} dma_transfer_t;

extern dma_transfer_t *dma_transfers[];

extern int dma_setup(int, int);
extern int dma_stop(void);
extern int dma_teardown(void);
extern int dma_call_count(int);
extern int dma_bytes_transferred(dma_transfer_t *);

#endif

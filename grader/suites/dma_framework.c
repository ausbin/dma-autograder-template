#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include "../uint.h"
#include "../dma.h"
#include "dma_framework.h"

#define MIN(a, b) (((a) < (b))? (a) : (a))

DMA_CONTROLLER student_scratch[4];
dma_transfer_t *dma_transfers[4];
static dma_transfer_t *current_dma_transfers[4];
static int simulate_dma_chan_mask;
static char *simulate_dma_buf;
static int simulate_dma_max;

static inline int simulate_dma_buf_size(void) {
    return 2 * simulate_dma_max - 2;
}

static void simulate_dma(int chan, dma_transfer_t *transfer) {
    // If we're not configured to simulate this bad boy, don't
    if (!(simulate_dma_chan_mask & (1 << chan)))
        return;

    // If they didn't set all the flags, bail
    if (transfer->state != (DMA_STATE_SRC | DMA_STATE_DST |
                            DMA_STATE_CNT | DMA_STATE_ON))
        return;

    const volatile void *s_start = simulate_dma_buf + simulate_dma_buf_size() - simulate_dma_max;
    volatile void *d_start = transfer->flags.dst;
    int n = transfer->flags.cnt & 0xFFFF;

    int src_bits = (transfer->flags.cnt >> 23) & 0x3;
    int s_inc = (src_bits == 0x2)? 0 : (src_bits == 0x1)? -1 : 1;
    int dst_bits = (transfer->flags.cnt >> 21) & 0x3;
    int d_inc = (dst_bits == 0x2)? 0 : (dst_bits == 0x1)? -1 : 1;

    int transfer32 = (transfer->flags.cnt >> 26) & 0x1;

    if (transfer32) {
        const volatile u32 *s = s_start;
        volatile u32 *d = d_start;
        int count = MIN(n, simulate_dma_max / 4);

        for (int i = 0; i < count; i++) {
            *d = *s;
            s += s_inc;
            d += d_inc;
        }
    } else {
        const volatile u16 *s = s_start;
        volatile u16 *d = d_start;
        int count = MIN(n, simulate_dma_max / 2);

        for (int i = 0; i < count; i++) {
            *d = *s;
            s += s_inc;
            d += d_inc;
        }
    }
}

static dma_transfer_t *add_transfer(int chan) {
    dma_transfer_t *transfer = calloc(sizeof (dma_transfer_t), 1);

    if (!transfer) {
        perror("calloc");
        return NULL;
    }

    if (!dma_transfers[chan]) {
        dma_transfers[chan] = transfer;
        current_dma_transfers[chan] = transfer;
    } else {
        current_dma_transfers[chan]->next = transfer;
        current_dma_transfers[chan] = transfer;
    }

    return transfer;
}

static void log_dma_access(enum state reg, int chan) {
    dma_transfer_t *transfer;
    // If the last DMA access was completed or this is the first, we
    // need a new one
    if (!current_dma_transfers[chan] ||
            (transfer = current_dma_transfers[chan])->state & DMA_STATE_ON) {
        transfer = add_transfer(chan);

        if (!transfer) {
            return;
        }
    }

    transfer->state |= reg;
    if (reg == DMA_STATE_SRC) {
        transfer->flags.src = student_scratch[chan].src;
    } else if (reg == DMA_STATE_DST) {
        transfer->flags.dst = student_scratch[chan].dst;
    } else if (reg == DMA_STATE_CNT) {
        transfer->flags.cnt = student_scratch[chan].cnt;

        if (transfer->flags.cnt & DMA_ON) {
            transfer->state |= DMA_STATE_ON;

            memset(&student_scratch[chan], 0, sizeof student_scratch[chan]);

            simulate_dma(chan, transfer);
        }
    }
}

// Why use mmap() for this instead of malloc()? Because we want the fork()ed
// child processes in speculatively_copy_src() to be able to write to this
// buffer too.
static void *map_simulate_dma_buf(void) {
    void *ret;
    if ((ret = mmap(NULL, simulate_dma_buf_size(), PROT_READ | PROT_WRITE,
                    MAP_SHARED | MAP_ANONYMOUS, -1, 0)) == MAP_FAILED)
        return NULL;
    return ret;
}

// Fork off child processes which _attempt_ to copy data around src.
// They'll copy as much as they can and then croak
static void speculatively_copy_src(dma_transfer_t *transfer) {
    // Zero out that son of a gun
    memset(simulate_dma_buf, 0, simulate_dma_buf_size());

    const char *src = (const char *) transfer->flags.src;

    // Copy RHS
    if (!fork()) {
        // child
        for (int i = 0; i < simulate_dma_max; i++)
            *(simulate_dma_buf + simulate_dma_buf_size() - simulate_dma_max + i) = src[i];

        _exit(0);
    }

    // Copy LHS
    if (!fork()) {
        // child
        for (int i = 0; i < simulate_dma_max - 2; i++)
            *(simulate_dma_buf + simulate_dma_buf_size() - simulate_dma_max - i) = src[-i];

        _exit(0);
    }

    // Wait for both to stop
    wait(NULL);
    wait(NULL);
}

static void log_if_diff(void) {
    for (int chan = 0; chan < 4; chan++) {
        dma_transfer_t *transfer = current_dma_transfers[chan];
        if (!transfer || transfer->state & DMA_STATE_ON) {
            if (student_scratch[chan].src) {
                log_dma_access(DMA_STATE_SRC, chan);
            }
            if (student_scratch[chan].dst) {
                log_dma_access(DMA_STATE_DST, chan);
            }
            if (student_scratch[chan].cnt) {
                log_dma_access(DMA_STATE_CNT, chan);
            }
        } else {
            if (transfer->flags.src != student_scratch[chan].src) {
                log_dma_access(DMA_STATE_SRC, chan);
            }
            if (transfer->flags.dst != student_scratch[chan].dst) {
                log_dma_access(DMA_STATE_DST, chan);
            }
            if (transfer->flags.cnt != student_scratch[chan].cnt) {
                log_dma_access(DMA_STATE_CNT, chan);
            }
        }

        // Check again. Might have been created by a log_dma_access() call above
        transfer = current_dma_transfers[chan];

        // If they _might_ be setting DMA_ON, copy everything within reach of
        // the src pointer
        if (transfer && transfer->state & DMA_STATE_SRC &&
                simulate_dma_chan_mask & (1 << chan) && simulate_dma_buf) {
            speculatively_copy_src(transfer);
        }
    }
}

// Called by the `DMA' macro in ../gba.h. That is, running `DMA[3].xxx = ...'
// invokes this because that expands to `_fake_dma()[3].xxx = ...', except this
// means we don't know what field the programmer will access.
DMA_CONTROLLER *_fake_dma(void) {
    log_if_diff();
    return student_scratch;
}

int dma_call_count(int chan) {
    int count = 0;

    for (dma_transfer_t *t = dma_transfers[chan]; t; t = t->next)
        count++;

    return count;
}

int dma_bytes_transferred(dma_transfer_t *transfer) {
    return (transfer->flags.cnt & 0xFFFF) << (((transfer->flags.cnt >> 26) & 0x1) + 1);
}

// simulate_dma_channel_mask is a bitmask where bit N is set if you want
// to simulate DMA transfers on channel N
int dma_setup(int simulate_dma_channel_mask, int simulate_max_trans_bytes) {
    simulate_dma_chan_mask = simulate_dma_channel_mask;
    simulate_dma_max = simulate_max_trans_bytes;

    if (simulate_dma_chan_mask && simulate_dma_max > 0) {
        if ((simulate_dma_buf = map_simulate_dma_buf()) == MAP_FAILED) {
            perror("mmap");
            return 0;
        }
    } else {
        simulate_dma_buf = NULL;
    }

    memset(dma_transfers, 0, sizeof dma_transfers);
    memset(student_scratch, 0, sizeof student_scratch);
    memset(current_dma_transfers, 0, sizeof current_dma_transfers);

    return 1;
}

int dma_stop(void) {
    // Final chance to check for any more transfers
    log_if_diff();

    if (simulate_dma_buf &&
        munmap(simulate_dma_buf, simulate_dma_buf_size()) == -1) {
        perror("munmap");
        return 0;
    }

    return 1;
}

int dma_teardown(void) {
    // Free the linked list of transfers
    for (int chan = 0; chan <= 3; chan++) {
        for (dma_transfer_t *trans = dma_transfers[chan]; trans;) {
            dma_transfer_t *tmp = trans->next;
            free(trans);
            trans = tmp;
        }

        dma_transfers[chan] = NULL;
        current_dma_transfers[chan] = NULL;
    }

    return 1;
}

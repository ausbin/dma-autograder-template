#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <signal.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include "../uint.h"
#include "../dma.h"
#include "dma_framework.h"

#define MIN(a, b) (((a) < (b))? (a) : (a))

const int fake_dma_page_size = sizeof (DMA_CONTROLLER) * 4;

// default sigsegv handler
static struct sigaction defaction;
void *volatile fake_dma_page;
// Alternate between these two
static void *fake_dma_pages[2];
static int fake_dma_index;
static void *last_dma_access;

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

// Even if it's the wrong channel, check that this access at least
// touches a valid src/dst/cnt
static int valid_dma_access(void *dma_controllers, void *access_addr,
                            int *chan_out, int *field_out) {
    DMA_CONTROLLER *dma = dma_controllers;
    for (int i = 0; i <= 3; i++) {
        uintptr_t src = (uintptr_t) &dma[i].src;
        uintptr_t dst = (uintptr_t) &dma[i].dst;
        uintptr_t cnt = (uintptr_t) &dma[i].cnt;
        uintptr_t access = (uintptr_t) access_addr;

        if (access == src) {
            *chan_out = i;
            *field_out = DMA_STATE_SRC;
            return 1;
        } else if (access == dst) {
            *chan_out = i;
            *field_out = DMA_STATE_DST;
            return 1;
        } else if (access == cnt) {
            *chan_out = i;
            *field_out = DMA_STATE_CNT;
            return 1;
        }
    }
    return 0;
}

static dma_transfer_t *add_transfer(int chan) {
    dma_transfer_t *transfer = calloc(sizeof (dma_transfer_t), 1);

    if (!transfer)
        return NULL;

    if (!dma_transfers[chan]) {
        dma_transfers[chan] = transfer;
        current_dma_transfers[chan] = transfer;
    } else {
        current_dma_transfers[chan]->next = transfer;
        current_dma_transfers[chan] = transfer;
    }

    return transfer;
}

static void log_dma_access(void *dma_controllers, void *access_addr) {
    if (!access_addr)
        return;

    DMA_CONTROLLER *dma = dma_controllers;
    uintptr_t access = (uintptr_t) access_addr;

    int chan = -1;

    // Identify the DMA channel
    for (int i = 3; i >= 0; i--) {
        if (access >= (uintptr_t)(dma + i)) {
            chan = i;
            break;
        }
    }

    // In theory, this is impossible because of valid_dma_access()
    // being called earlier, but check just in case
    if (chan == -1) {
        // TODO: figure out how to handle errors like this in a
        // signal handler
        return;
    }

    uintptr_t src = (uintptr_t) &dma[chan].src;
    uintptr_t dst = (uintptr_t) &dma[chan].dst;
    uintptr_t cnt = (uintptr_t) &dma[chan].cnt;

    // Should also be impossible because of valid_dma_access(), but
    // students are living stress testers
    if (access != src && access != dst && access != cnt) {
        // TODO: figure out how to handle errors like this in a
        // signal handler
        return;
    }

    dma_transfer_t *transfer;
    // If the last DMA access was completed or this is the first, we
    // need a new one
    if (!current_dma_transfers[chan] ||
            (transfer = current_dma_transfers[chan])->state & DMA_STATE_ON) {
        transfer = add_transfer(chan);

        if (!transfer)
            // TODO: figure out how to handle errors like this in a
            // signal handler
            return;
    }

    if (access == src) {
        transfer->state |= DMA_STATE_SRC;
        transfer->flags.src = dma[chan].src;
    } else if (access == dst) {
        transfer->state |= DMA_STATE_DST;
        transfer->flags.dst = dma[chan].dst;
    } else if (access == cnt) {
        transfer->state |= DMA_STATE_CNT;
        transfer->flags.cnt = dma[chan].cnt;

        if (dma[chan].cnt & DMA_ON) {
            transfer->state |= DMA_STATE_ON;

            simulate_dma(chan, transfer);
        }
    }
}

static int unregister_sighandler(void) {
    if (sigaction(SIGSEGV, &defaction, NULL) == -1) {
        perror("sigaction");
        return 0;
    }
    return 1;
}

static void zero_dma_controller(void *page) {
    memset(page, 0, sizeof (DMA_CONTROLLER) * 4);
}

// Fork off child processes which _attempt_ to copy data around src.
// They'll copy as much as they can and then croak
static void copy_src_to_buf(dma_transfer_t *transfer) {
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

// We memset each DMA_CONTROLLER to 0 on every segfault, so put it back
// in case they're reading it
static int restore_field(void *dma_controllers, void *access_addr) {
    int chan, field;
    if (!valid_dma_access(dma_controllers, access_addr, &chan, &field))
        return 0;

    // Only copy if was set before
    dma_transfer_t *transfer;
    if ((transfer = current_dma_transfers[chan]) && (transfer->state & field)) {
        DMA_CONTROLLER *dma = dma_controllers;
        dma += chan;

        if (field == DMA_STATE_SRC)
            dma->src = transfer->flags.src;
        else if (field == DMA_STATE_DST)
            dma->dst = transfer->flags.dst;
        else if (field == DMA_STATE_CNT)
            dma->cnt = transfer->flags.cnt;
    }

    // If they might be setting DMA_ON, copy everything within reach of
    // the src pointer
    if (field == DMA_STATE_CNT && transfer &&
        transfer->state & DMA_STATE_SRC && simulate_dma_chan_mask & (1 << chan) &&
        simulate_dma_buf)
        copy_src_to_buf(transfer);

    return 1;
}

static void handle_sigsegv(int signal, siginfo_t *info, void *ctx) {
    if (signal != SIGSEGV)
        return;
    // unused argument
    (void)ctx;

    // backup the current page so we can use it later, after we reassign
    // fake_dma_page
    void *volatile this_dma_page = fake_dma_page;

    // now, for handoff back to the user, make writable so we don't double fault
    mprotect(this_dma_page, fake_dma_page_size, PROT_READ | PROT_WRITE);

    // the old page address is safely in a register, so change where
    // fake_dma_page points. this way, the next `DMA[...] = ...' will fault
    fake_dma_index = ~fake_dma_index & 0x1;
    fake_dma_page = fake_dma_pages[fake_dma_index];
    log_dma_access(fake_dma_page, last_dma_access);
    if (last_dma_access)
        zero_dma_controller(fake_dma_page);
    mprotect(fake_dma_page, fake_dma_page_size, PROT_NONE);

    // In case they're reading, put the value they want back in memory
    if (!restore_field(this_dma_page, info->si_addr)) {
        // if this is an invalid access, give up and let the default signal
        // handler take over
        // TODO: Don't perror() in here, since you're not supposed to do
        //       that in signal handlers
        unregister_sighandler();
        return;
    }

    last_dma_access = info->si_addr;
}

static int register_sighandler(void) {
    struct sigaction action;
    action.sa_sigaction = handle_sigsegv;
    action.sa_flags = SA_SIGINFO;
    sigemptyset(&action.sa_mask);

    if (sigaction(SIGSEGV, &action, &defaction) == -1) {
        perror("sigaction");
        return 0;
    }
    return 1;
}

static void *map_fake_dma_page(void) {
    void *ret;
    if ((ret = mmap(NULL, fake_dma_page_size, PROT_NONE,
                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)) == MAP_FAILED)
        return NULL;
    return ret;
}

static void *map_simulate_dma_buf(void) {
    void *ret;
    if ((ret = mmap(NULL, simulate_dma_buf_size(), PROT_READ | PROT_WRITE,
                    MAP_SHARED | MAP_ANONYMOUS, -1, 0)) == MAP_FAILED)
        return NULL;
    return ret;
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

    memset(dma_transfers, 0, sizeof (dma_transfers));
    memset(current_dma_transfers, 0, sizeof (current_dma_transfers));

    // Set the second page as writable so that when we do the magic swap
    // in the signal handler, we don't segfault when we zero it out
    if (!(fake_dma_pages[0] = map_fake_dma_page()) ||
        !(fake_dma_pages[1] = map_fake_dma_page()))
        return 0;

    fake_dma_page = fake_dma_pages[0];

    if (!register_sighandler())
        return 0;

    return 1;
}

int dma_stop(void) {
    if (!unregister_sighandler())
        return 0;

    // Need to choose the opposite page of the current since we swap
    // the two in the page fault handler
    log_dma_access(fake_dma_pages[~fake_dma_index & 0x1], last_dma_access);

    // Don't need these fellas anymore
    if (munmap(fake_dma_pages[0], fake_dma_page_size) == -1 ||
        munmap(fake_dma_pages[1], fake_dma_page_size) == -1) {
        perror("munmap");
        return 0;
    }

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

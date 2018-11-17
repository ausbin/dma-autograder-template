#include <stdlib.h>
#include <check.h>
#include "dma_regions.h"
#include "dma_framework.h"
#include "test_images.h"
#include "../gba.h"

static int dma_region_cmp(const void *leftp, const void *rightp) {
    const dma_region_t *left = leftp;
    const dma_region_t *right = rightp;
    return (int)left->src_start - (int)right->src_start;
}

static void swap_uint(unsigned int *a, unsigned int *b) {
    unsigned int tmp = *a;
    *a = *b;
    *b = tmp;
}

void dma_regions_normalize(dma_region_t *arr, int len) {
    for (int i = 0; i < len; i++) {
        if (arr[i].src_start > arr[i].src_end) {
            swap_uint(&arr[i].src_start, &arr[i].src_end);
            swap_uint(&arr[i].dst_start, &arr[i].dst_end);
        }
    }

    qsort(arr, len, sizeof (dma_region_t), dma_region_cmp);
}

static void extract_cnt_flags(unsigned int cnt,
                              unsigned int *n_out,
                              unsigned int *src_cnt_out,
                              unsigned int *dst_cnt_out,
                              unsigned int *chunk32_out) {
    *n_out = cnt & 0xffff;
    *src_cnt_out = (cnt >> 23) & 0x3;
    *dst_cnt_out = (cnt >> 21) & 0x3;
    *chunk32_out = (cnt >> 26) & 0x1;
}

void dma_regions_from_transfers(int chan, dma_transfer_t *transfer,
                                dma_region_t **arr_out, int *len_out) {
    unsigned int i = 0;
    dma_region_t *arr = NULL;
    unsigned int cap = 0;

    while (transfer) {
        if (i == cap) {
            arr = realloc(arr, (cap = cap * 2 + 1) * sizeof (dma_region_t));
            ck_assert(arr);
        }

        if (!(transfer->state & DMA_STATE_SRC))
            ck_abort_msg("in DMA transfer #%d on channel %d, src register was "
                         "never assigned!", i + 1, chan);
        if (!(transfer->state & DMA_STATE_DST))
            ck_abort_msg("in DMA transfer #%d on channel %d, dst register was "
                         "never assigned!", i + 1, chan);
        if (!(transfer->state & DMA_STATE_CNT))
            ck_abort_msg("in DMA transfer #%d on channel %d, cnt register was "
                         "never assigned!", i + 1, chan);
        if (!(transfer->state & DMA_STATE_ON))
            ck_abort_msg("in DMA transfer #%d on channel %d, DMA_ON bit in cnt "
                         "register was never set!", i + 1, chan);

        if ((uintptr_t) transfer->flags.src < (uintptr_t) image)
            ck_abort_msg("in DMA transfer #%d on channel %d, address in src "
                         "register is before beginning of image!", i + 1, chan);
        if ((uintptr_t) transfer->flags.src >= (uintptr_t) (image + IMAGE_WIDTH * IMAGE_HEIGHT))
            ck_abort_msg("in DMA transfer #%d on channel %d, address in src "
                         "register is beyond end of image!", i + 1, chan);

        if ((uintptr_t) transfer->flags.dst < (uintptr_t) videoBuffer)
            ck_abort_msg("in DMA transfer #%d on channel %d, address in src "
                         "register is before beginning of videoBuffer!", i + 1, chan);
        if ((uintptr_t) transfer->flags.dst >= (uintptr_t) (videoBuffer + 240 * 160))
            ck_abort_msg("in DMA transfer #%d on channel %d, address in dst "
                         "register is beyond end of videoBuffer!", i + 1, chan);

        if ((uintptr_t) transfer->flags.src % 2 != (uintptr_t) image % 2)
            ck_abort_msg("in DMA transfer #%d on channel %d, address in src "
                         "register is not halfword aligned!", i + 1, chan);
        if ((uintptr_t) transfer->flags.dst % 2 != (uintptr_t) videoBuffer % 2)
            ck_abort_msg("in DMA transfer #%d on channel %d, address in dst "
                         "register is not halfword aligned!", i + 1, chan);

        // Ok, these guys are ok now!
        unsigned int src_start = (const u16 *) transfer->flags.src - image;
        unsigned int dst_start = (u16 *) transfer->flags.dst - videoBuffer;

        unsigned int n, src_cnt, dst_cnt, chunk32;
        extract_cnt_flags(transfer->flags.cnt, &n, &src_cnt, &dst_cnt, &chunk32);

        if (!n)
            ck_abort_msg("in DMA transfer #%d on channel %d is n = 0 transfer, "
                         "invalid", i + 1, chan);

        if (src_cnt == 0x3)
            ck_abort_msg("in DMA transfer #%d on channel %d attempts to use "
                         "DESTINATION_RESET for src. please reconsider your life",
                         i + 1, chan);
        if (dst_cnt == 0x3)
            ck_abort_msg("in DMA transfer #%d on channel %d attempts to use "
                         "DESTINATION_RESET. not supported", i + 1, chan);

        if (chunk32)
            n *= 2;

        // Do bounds checking on N
        if (src_cnt == 0x0 &&
            (uintptr_t) ((u16 *) transfer->flags.src + n) > (uintptr_t) (image + IMAGE_WIDTH * IMAGE_HEIGHT))
            ck_abort_msg("DMA transfer #%d on channel %d attempts to copy "
                         "beyond the end of the image!", i + 1, chan);
        if (src_cnt == 0x1 && n > src_start + 1)
            ck_abort_msg("DMA transfer #%d on channel %d attempts to copy "
                         "past the beginning of the image!", i + 1, chan);
        if (dst_cnt == 0x0 &&
            (uintptr_t) ((u16 *) transfer->flags.dst + n) > (uintptr_t) (videoBuffer + 240 * 160))
            ck_abort_msg("DMA transfer #%d on channel %d attempts to copy "
                         "beyond the end of the videoBuffer!", i + 1, chan);
        if (dst_cnt == 0x1 && n > dst_start + 1)
            ck_abort_msg("DMA transfer #%d on channel %d attempts to copy "
                         "past the beginning of the videoBuffer!", i + 1, chan);

        int src_sign = src_cnt? -1 : 1;
        unsigned int src_end = (src_cnt == 0x2)? src_start : src_start + src_sign * (n - 1);
        int dst_sign = dst_cnt? -1 : 1;
        // Don't ask me why you'd use DESTINATION_FIXED but it's 1am and
        // I don't give a shit at this point
        unsigned int dst_end = (dst_cnt == 0x2)? dst_start : dst_start + dst_sign * (n - 1);

        arr[i].original_index = i;
        arr[i].src_start = src_start;
        arr[i].src_end = src_end;
        arr[i].dst_start = dst_start;
        arr[i].dst_end = dst_end;

        transfer = transfer->next;
        i++;
    }

    *arr_out = arr;
    *len_out = i;
}

#define MIN(a, b) (((a) < (b))? (a) : (b))
#define IRC(i) ((i) / IMAGE_WIDTH), ((i) % IMAGE_WIDTH)
#define VRC(i) ((i) / 240), ((i) % 240)

void dma_regions_assert(dma_region_t *actual, int actual_len,
                        dma_region_t *expected, int expected_len) {

    // First, try to find a difference in the sequence
    for (int i = 0; i < MIN(actual_len, expected_len); i++) {
        if (actual[i].src_start != expected[i].src_start ||
            actual[i].src_end != expected[i].src_end)
            ck_abort_msg("DMA transfer #%d has wrong range of source addresses. "
                         "actual: (%d,%d)->(%d,%d). expected: (%d,%d)->(%d,%d)",
                         actual[i].original_index + 1,
                         IRC(actual[i].src_start), IRC(actual[i].src_end),
                         IRC(expected[i].src_start), IRC(expected[i].src_end));

        if (actual[i].dst_start != expected[i].dst_start ||
            actual[i].dst_end != expected[i].dst_end)
            ck_abort_msg("DMA transfer #%d has wrong range of destination addresses. "
                         "actual: (%d,%d)->(%d,%d). expected: (%d,%d)->(%d,%d)",
                         actual[i].original_index + 1,
                         VRC(actual[i].dst_start), VRC(actual[i].dst_end),
                         VRC(expected[i].dst_start), VRC(expected[i].dst_end));
    }

    if (actual_len > expected_len)
        ck_abort_msg("found %d extra dma transfers", actual_len - expected_len);
    if (actual_len < expected_len)
        ck_abort_msg("expected %d more dma transfers", expected_len - actual_len);
}

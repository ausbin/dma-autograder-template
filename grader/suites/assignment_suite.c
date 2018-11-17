// Assignment Tester

#include <check.h>
#include <stdlib.h>
#include "../assignment.h"
#include "dma_framework.h"
#include "test_images.h"
#include "dma_regions.h"

// I apologize to my family for this
#define tcase_hack(suite, setup_fixture, teardown_fixture, func) { \
    TCase *tc = tcase_create(#func); \
    tcase_add_checked_fixture(tc, setup_fixture, teardown_fixture); \
    tcase_add_test(tc, func); \
    suite_add_tcase(s, tc); \
}

static u16 videobuf[38400];
volatile u16 *const videoBuffer = videobuf;

static void setup_dma_nosim_fixture(void) {
    // Don't even simulate DMA calls, just log them
    dma_setup(0, 0);
}

static void setup_dma_sim_fixture(void) {
    // Max amount they should copy in bytes is the size of the
    // videoBuffer. And only simulate channel 3 because other ones won't
    // even work anyway
    dma_setup(DMA_SIM_CHAN_3, 240 * 160 * 2);
    // Make videobuf black
    memset(videobuf, 0, sizeof (videobuf));
}

static void teardown_dma_fixture(void) {
    dma_teardown();
}

static void call_student_code(void (*func)(const u16 *frame, int width, int height)) {
    func(image, IMAGE_WIDTH, IMAGE_HEIGHT);
    dma_stop();
}

static u16 *image_from_videobuf(void) {
    u16 *im = malloc(sizeof (u16) * IMAGE_WIDTH * IMAGE_HEIGHT);
    ck_assert(im);
    for (int r = 0; r < IMAGE_HEIGHT; r++)
        memcpy(im + r * IMAGE_WIDTH, videobuf + r * 240, IMAGE_WIDTH * sizeof (u16));
    return im;
}

char *image_str(const u16 *im, int emphasis) {
    char *str = malloc(IMAGE_WIDTH * IMAGE_HEIGHT * 4);
    ck_assert(str);

    int j = 0;
    for (int i = 0; i < IMAGE_WIDTH * IMAGE_HEIGHT; i++) {
        if (i % IMAGE_WIDTH == 0) {
            str[j++] = (i == emphasis)? '>' : ' ';
        }

        int validchar = (im[i] >= 'a' && im[i] <= 'z') ||
                        (im[i] >= 'A' && im[i] <= 'Z');
        char c = validchar? im[i] : '?';
        str[j++] = c;

        // NUMBER THEORY BABY
        // LARRY ROLEN #1 WINGMAN
        if (i % IMAGE_WIDTH == IMAGE_WIDTH - 1) {
            if (i == emphasis)
                str[j++] = '<';

            if (i < IMAGE_WIDTH * IMAGE_HEIGHT - 1) {
                str[j++] = '\n';
                str[j++] = '\n';
            }
        } else {
            str[j++] = (i == emphasis)? '<' : ' ';
        }
    }

    str[j] = '\0';

    return str;
}

void assert_image_eq(const u16 *actual, const u16 *expected) {
    int i;

    for (i = 0; i < IMAGE_WIDTH * IMAGE_WIDTH; i++)
        if (actual[i] != expected[i])
            goto bad;

    return;

    bad:
    ck_abort_msg("actual image does not match expected! differs at row %d, col %d.\n"
                 "\n"
                 "actual:\n"
                 "%s"
                 "\n"
                 "\n"
                 "expected:\n"
                 "%s",
                 i / IMAGE_WIDTH, i % IMAGE_WIDTH,
                 image_str(actual, i), image_str(expected, i));
}

void assert_videobuf_unmangled(void) {
    int r, c;

    for (r = 0; r < IMAGE_HEIGHT; r++)
        for (c = IMAGE_WIDTH; c < 240; c++)
            if (videobuf[r * 240 + c])
                goto bad;

    for (r = IMAGE_HEIGHT; r < 160; r++)
        for (c = 0; c < 240; c++)
            if (videobuf[r * 240 + c])
                goto bad;

    return;

    bad:
    ck_abort_msg("videoBuffer was modifed outside image at row %d, col %d!", r, c);
}

static void videobuf_test(void (*func)(const u16 *frame, int width, int height),
                          const u16 *expected) {
    call_student_code(func);

    assert_videobuf_unmangled();

    u16 *actual = image_from_videobuf();
    assert_image_eq(actual, expected);
    free(actual);
}

static void transfers_test(void (*func)(const u16 *frame, int width, int height),
                           const dma_region_t *expected_arr) {
    call_student_code(func);

    for (int chan = 0; chan <= 2; chan++)
        if (dma_transfers[chan])
            ck_abort_msg("attempted DMA transfer on channel %d! "
                         "that channel is not for graphics", chan);

    dma_region_t *actual;
    int actual_len;
    dma_regions_from_transfers(3, dma_transfers[3], &actual, &actual_len);
    dma_regions_normalize(actual, actual_len);

    // We copy a row at a time, so each set of transfers has a length of
    // the height of the image
    int expected_len = IMAGE_HEIGHT;
    dma_region_t *expected = malloc(expected_len * sizeof (dma_region_t));
    ck_assert(expected);
    memcpy(expected, expected_arr, expected_len * sizeof (dma_region_t));
    dma_regions_normalize(expected, expected_len);

    dma_regions_assert(actual, actual_len, expected, expected_len);
    free(actual);
    free(expected);
}

START_TEST(test_drawImage3_videobuf) {
    videobuf_test(drawImage3, image);
}
END_TEST

START_TEST(test_drawImage3_transfers) {
    transfers_test(drawImage3, image_regions);
}
END_TEST

Suite *assignment_suite(void) {
    Suite *s = suite_create("fun assignment");

    tcase_hack(s, setup_dma_sim_fixture, teardown_dma_fixture, test_drawImage3_videobuf);
    tcase_hack(s, setup_dma_nosim_fixture, teardown_dma_fixture, test_drawImage3_transfers);

    return s;
}

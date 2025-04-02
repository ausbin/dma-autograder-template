#ifndef TEST_IMAGES_H
#define TEST_IMAGES_H

#include "dma_regions.h"

#define IMAGE_WIDTH 5
#define IMAGE_HEIGHT 7

extern const u16 image[IMAGE_WIDTH * IMAGE_HEIGHT];
extern const dma_region_t image_regions[IMAGE_HEIGHT];

#endif

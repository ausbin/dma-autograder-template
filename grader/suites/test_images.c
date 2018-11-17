#include "../uint.h"
#include "test_images.h"
#include "dma_regions.h"

#define II(r, c) ((r) * IMAGE_WIDTH + (c))
#define VI(r, c) ((r) * 240 + (c))

const u16 image[] = {
    'a', 'b', 'c', 'd', 'e',
    'f', 'g', 'h', 'i', 'j',
    'k', 'l', 'm', 'n', 'o',
    'p', 'q', 'r', 's', 't',
    'u', 'v', 'w', 'x', 'y',
    'A', 'B', 'C', 'D', 'E',
    'F', 'G', 'H', 'I', 'J',
};

const dma_region_t image_regions[] = {
    {0, II(0,0), II(0,4), VI(0,0), VI(0,4)},
    {0, II(1,0), II(1,4), VI(1,0), VI(1,4)},
    {0, II(2,0), II(2,4), VI(2,0), VI(2,4)},
    {0, II(3,0), II(3,4), VI(3,0), VI(3,4)},
    {0, II(4,0), II(4,4), VI(4,0), VI(4,4)},
    {0, II(5,0), II(5,4), VI(5,0), VI(5,4)},
    {0, II(6,0), II(6,4), VI(6,0), VI(6,4)},
};

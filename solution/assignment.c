#include "gba.h"

/**
 * Draw an image in the top-left corner of the screen.
 *
 * You MUST use DMA and the minimum number of DMA calls possible.
 */
void drawImage3(const u16 *image, int width, int height)
{
    for (int r = 0; r < height; r++) {
        DMA[3].src = image + r * width;
        DMA[3].dst = videoBuffer + r * WIDTH;
        DMA[3].cnt = DMA_ON | width;
    }
}

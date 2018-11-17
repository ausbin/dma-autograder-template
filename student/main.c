#include "gba.h"
#include "assignment.h"
#include "skittles.h"
#include <stdio.h>

int main(void)
{
    // Set GBA mode 3
    REG_DISPCNT = MODE3 | BG2_ENABLE;

    // Draw my staple in college
    drawImage3(skittles, SKITTLES_WIDTH, SKITTLES_HEIGHT);

    while (1);
    return 0;
}

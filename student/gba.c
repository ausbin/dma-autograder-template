#include "gba.h"

#include <stdio.h>

volatile unsigned short *videoBuffer = (volatile unsigned short *)0x6000000;

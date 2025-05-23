#include <stdio.h>
#include <grass/gis.h>
#include "global.h"

char *print_time(time_t t)
{
    int H, M, S;
    static char outstr[20];

    if (t < 0)
        t = 0;

    H = t / 3600;
    t -= H * 3600;
    M = t / 60;
    S = t - M * 60;

    if (H) {
        snprintf(outstr, sizeof(outstr), "%dh%02dm%02ds", H, M, S);
    }
    else if (M) {
        snprintf(outstr, sizeof(outstr), "%dm%02ds", M, S);
    }
    else {
        snprintf(outstr, sizeof(outstr), "%ds", S);
    }

    return outstr;
}

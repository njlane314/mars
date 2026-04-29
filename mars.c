/*
 * mars.c -- UNIX command entrypoint for mars.
 *
 * Keep this file plain C and boring. The implementation lives behind the
 * C-compatible interface in mars_api.h.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mars_api.h"

static void usage(const char *argv0)
{
    (void)fprintf(stderr,
                  "usage:\n"
                  "  %s fit    input.csv model.mars\n"
                  "  %s replay input.csv model.mars trades.csv\n"
                  "  %s inspect model.mars\n\n"
                  "input csv schema:\n"
                  "  ts,bid,ask,bid_sz,ask_sz,volume\n\n"
                  "defaults: ES/MES-like 1-second bars, horizon=%u bars, tick=%.8f\n",
                  argv0, argv0, argv0, RT_DEFAULT_HORIZON, RT_DEFAULT_TICK_SIZE);
}

int main(int argc, char **argv)
{
    rt_status_t st;

    if (argc < 2) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    if (strcmp(argv[1], "fit") == 0) {
        if (argc != 4) {
            usage(argv[0]);
            return EXIT_FAILURE;
        }
        st = mars_fit(argv[2], argv[3]);
    } else if (strcmp(argv[1], "replay") == 0) {
        if (argc != 5) {
            usage(argv[0]);
            return EXIT_FAILURE;
        }
        st = mars_replay(argv[2], argv[3], argv[4]);
    } else if (strcmp(argv[1], "inspect") == 0) {
        if (argc != 3) {
            usage(argv[0]);
            return EXIT_FAILURE;
        }
        st = mars_inspect(argv[2]);
    } else {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    if (st != RT_OK) {
        (void)fprintf(stderr, "mars: error %d\n", (int)st);
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

/*
 * main.c -- UNIX command entrypoint for mars.
 *
 * Keep this file plain C and boring. The implementation lives behind the
 * C-compatible interface in api.h.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "api.h"

static void usage(const char *argv0)
{
    (void)fprintf(stderr,
                  "usage:\n"
                  "  %s fit-db market.db model.mars [table]\n"
                  "  %s replay-db market.db model.mars trades.csv [table]\n"
                  "  %s inspect model.mars\n\n"
                  "default db table: market_bars\n"
                  "defaults: ES/MES-like 1-second bars, horizon=%u bars, tick=%.8f\n",
                  argv0, argv0, argv0,
                  MARS_DEFAULT_HORIZON, MARS_DEFAULT_TICK_SIZE);
    (void)fprintf(stderr,
                  "\ndb commands:\n"
                  "  %s db-init market.db\n"
                  "  %s eth-update market.db FROM_BLOCK TO_BLOCK [--blocks-only]\n"
                  "  %s eth-export market.db ethblocks.csv\n"
                  "  %s fred-update market.db SERIES[,SERIES...]\n"
                  "  %s fred-export market.db fred.csv\n\n"
                  "env:\n"
                  "  ETH_RPC_URL     Ethereum JSON-RPC endpoint for eth-update\n"
                  "  FRED_API_KEY    FRED API key for fred-update\n",
                  argv0, argv0, argv0, argv0, argv0);
}

static int parse_block_arg(const char *s, uint64_t *out)
{
    char *endp = NULL;
    unsigned long long v;

    if ((s == NULL) || (out == NULL)) {
        return 0;
    }
    if (strcmp(s, "latest") == 0) {
        *out = MARS_ETH_LATEST;
        return 1;
    }

    errno = 0;
    v = strtoull(s, &endp, 10);
    if ((errno != 0) || (endp == s) || (*endp != '\0')) {
        return 0;
    }

    *out = (uint64_t)v;
    return 1;
}

int main(int argc, char **argv)
{
    mars_status_t st;

    if (argc < 2) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    if (strcmp(argv[1], "fit-db") == 0) {
        if ((argc != 4) && (argc != 5)) {
            usage(argv[0]);
            return EXIT_FAILURE;
        }
        st = mars_fit_db(argv[2], (argc == 5) ? argv[4] : NULL, argv[3]);
    } else if (strcmp(argv[1], "replay-db") == 0) {
        if ((argc != 5) && (argc != 6)) {
            usage(argv[0]);
            return EXIT_FAILURE;
        }
        st = mars_replay_db(argv[2], (argc == 6) ? argv[5] : NULL, argv[3], argv[4]);
    } else if (strcmp(argv[1], "inspect") == 0) {
        if (argc != 3) {
            usage(argv[0]);
            return EXIT_FAILURE;
        }
        st = mars_inspect(argv[2]);
    } else if (strcmp(argv[1], "db-init") == 0) {
        if (argc != 3) {
            usage(argv[0]);
            return EXIT_FAILURE;
        }
        st = mars_db_init(argv[2]);
    } else if (strcmp(argv[1], "eth-update") == 0) {
        const char *rpc_url;
        uint64_t from_block;
        uint64_t to_block;
        uint32_t store_txs = 1U;

        if ((argc != 5) && (argc != 6)) {
            usage(argv[0]);
            return EXIT_FAILURE;
        }
        if ((parse_block_arg(argv[3], &from_block) == 0) ||
            (parse_block_arg(argv[4], &to_block) == 0)) {
            usage(argv[0]);
            return EXIT_FAILURE;
        }
        if (argc == 6) {
            if (strcmp(argv[5], "--blocks-only") != 0) {
                usage(argv[0]);
                return EXIT_FAILURE;
            }
            store_txs = 0U;
        }
        rpc_url = getenv("ETH_RPC_URL");
        if ((rpc_url == NULL) || (rpc_url[0] == '\0')) {
            (void)fprintf(stderr, "mars: ETH_RPC_URL is not set\n");
            return EXIT_FAILURE;
        }
        st = mars_eth_update(argv[2], rpc_url, from_block, to_block, store_txs);
    } else if (strcmp(argv[1], "eth-export") == 0) {
        if (argc != 4) {
            usage(argv[0]);
            return EXIT_FAILURE;
        }
        st = mars_eth_export(argv[2], argv[3]);
    } else if (strcmp(argv[1], "fred-update") == 0) {
        const char *api_key;

        if (argc != 4) {
            usage(argv[0]);
            return EXIT_FAILURE;
        }
        api_key = getenv("FRED_API_KEY");
        if ((api_key == NULL) || (api_key[0] == '\0')) {
            (void)fprintf(stderr, "mars: FRED_API_KEY is not set\n");
            return EXIT_FAILURE;
        }
        st = mars_fred_update(argv[2], argv[3], api_key);
    } else if (strcmp(argv[1], "fred-export") == 0) {
        if (argc != 4) {
            usage(argv[0]);
            return EXIT_FAILURE;
        }
        st = mars_fred_export(argv[2], argv[3]);
    } else {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    if (st != MARS_OK) {
        (void)fprintf(stderr, "mars: error %d\n", (int)st);
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

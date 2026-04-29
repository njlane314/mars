#ifndef MARS_DEX_HPP
#define MARS_DEX_HPP

#include <sqlite3.h>

#include "api.h"

class dex final {
public:
    dex() = delete;

    static mars_status_t schema(sqlite3 *db);
    static mars_status_t update(const char *db_path, const char *rpc_url,
                                uint64_t from_block, uint64_t to_block,
                                const char *pool);
    static mars_status_t export_csv(const char *db_path, const char *out_path,
                                    const char *pool);
};

#endif

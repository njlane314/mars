#ifndef MARS_DATA_HPP
#define MARS_DATA_HPP

#include "api.h"

class Data final {
public:
    Data() = delete;

    static mars_status_t loadBars(const char *db_path, const char *table, mars_data_t *data);
    static void release(mars_data_t *data);
};

#endif

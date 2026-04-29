#ifndef MARS_DATA_HPP
#define MARS_DATA_HPP

#include "api.h"

class data final {
public:
    data() = delete;

    static mars_status_t load_bars(const char *db_path, const char *table, mars_data_t *data);
    static void release(mars_data_t *data);
};

#endif

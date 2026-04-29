#ifndef CSV_HPP
#define CSV_HPP

#include "mars_api.h"

class CsvData final {
public:
    CsvData() = delete;

    static rt_status_t load(const char *path, rt_data_t *data);
    static void release(rt_data_t *data);
};

#endif

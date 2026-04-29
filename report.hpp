#ifndef REPORT_HPP
#define REPORT_HPP

#include "mars_api.h"

class Reporter final {
public:
    Reporter() = delete;

    static void printStats(const char *name, const rt_bt_stats_t *stats);
    static rt_status_t inspectModel(const char *model_path);
};

#endif

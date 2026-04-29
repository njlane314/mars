#ifndef REPORT_HPP
#define REPORT_HPP

#include "api.h"

class Reporter final {
public:
    Reporter() = delete;

    static void printStats(const char *name, const mars_bt_stats_t *stats);
    static mars_status_t inspectModel(const char *model_path);
};

#endif

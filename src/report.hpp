#ifndef REPORT_HPP
#define REPORT_HPP

#include "api.h"

class report final {
public:
    report() = delete;

    static void print_stats(const char *name, const mars_bt_stats_t *stats);
    static mars_status_t inspect_model(const char *model_path);
};

#endif

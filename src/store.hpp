#ifndef STORE_HPP
#define STORE_HPP

#include "api.h"

class store final {
public:
    store() = delete;

    static mars_config_t default_config();
    static mars_status_t init_from_config(mars_model_t *model, const mars_config_t *config, uint32_t k);
    static mars_status_t save(const char *path, const mars_model_t *model);
    static mars_status_t load(const char *path, mars_model_t *model);
    static double select_score(const mars_bt_stats_t *stats);
};

#endif

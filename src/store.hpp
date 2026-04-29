#ifndef STORE_HPP
#define STORE_HPP

#include "api.h"

class ModelStore final {
public:
    ModelStore() = delete;

    static mars_config_t defaultConfig();
    static mars_status_t initFromConfig(mars_model_t *model, const mars_config_t *config, uint32_t k);
    static mars_status_t save(const char *path, const mars_model_t *model);
    static mars_status_t load(const char *path, mars_model_t *model);
    static double selectScore(const mars_bt_stats_t *stats);
};

#endif

#ifndef MODEL_STORE_HPP
#define MODEL_STORE_HPP

#include "mars_api.h"

class ModelStore final {
public:
    ModelStore() = delete;

    static rt_config_t defaultConfig();
    static rt_status_t initFromConfig(rt_model_t *model, const rt_config_t *config, uint32_t k);
    static rt_status_t save(const char *path, const rt_model_t *model);
    static rt_status_t load(const char *path, rt_model_t *model);
    static double selectScore(const rt_bt_stats_t *stats);
};

#endif

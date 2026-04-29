#ifndef FEATURES_HPP
#define FEATURES_HPP

#include "api.h"

class features final {
public:
    features() = delete;

    static mars_status_t make(mars_data_t *data, const mars_config_t *config);
};

#endif

#ifndef FEATURES_HPP
#define FEATURES_HPP

#include "mars_api.h"

class FeatureBuilder final {
public:
    FeatureBuilder() = delete;

    static rt_status_t make(rt_data_t *data, const rt_config_t *config);
};

#endif

#ifndef MARS_TARGET_HPP
#define MARS_TARGET_HPP

#include "api.h"

class target final {
public:
    target() = delete;

    static mars_status_t make(mars_data_t *data, const mars_config_t *config);
};

#endif

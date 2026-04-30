#ifndef MARS_COST_HPP
#define MARS_COST_HPP

#include "api.h"

class cost final {
public:
    cost() = delete;

    static double threshold(const mars_model_t *model, const mars_row_t *row);
    static double position(const mars_model_t *model, const mars_row_t *row, double forecast_ticks);
};

#endif

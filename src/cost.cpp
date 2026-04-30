#include "cost.hpp"

double cost::threshold(const mars_model_t *m, const mars_row_t *r)
{
    double risk = 0.0;

    if ((m == NULL) || (r == NULL)) {
        return 0.0;
    }
    if (r->risk_ticks > 0.0) {
        risk = m->risk_lambda * r->risk_ticks;
    }
    return m->edge_cost_ticks + m->buffer_ticks + risk;
}

double cost::position(const mars_model_t *m, const mars_row_t *r, double forecast_ticks)
{
    const double band = threshold(m, r);

    if ((m == NULL) || (r == NULL)) {
        return 0.0;
    }
    if (r->spread_ticks > m->max_spread_ticks) {
        return 0.0;
    }
    if (forecast_ticks > band) {
        return m->pos_max;
    }
    if (forecast_ticks < -band) {
        return -m->pos_max;
    }
    return 0.0;
}

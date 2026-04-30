#include <float.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "store.hpp"

namespace {

static uint32_t model_aug_dim(uint32_t base_dim, uint32_t k)
{
    return base_dim + k + (base_dim * k);
}

static uint32_t env_u32(const char *name, uint32_t fallback, uint32_t lo, uint32_t hi)
{
    const char *s = getenv(name);
    char *endp = NULL;
    unsigned long v;

    if ((s == NULL) || (s[0] == '\0')) {
        return fallback;
    }

    errno = 0;
    v = strtoul(s, &endp, 10);
    if ((errno != 0) || (endp == s) || (*endp != '\0') || (v < lo) || (v > hi)) {
        return fallback;
    }
    return (uint32_t)v;
}

static double env_double(const char *name, double fallback, double lo, double hi)
{
    const char *s = getenv(name);
    char *endp = NULL;
    double v;

    if ((s == NULL) || (s[0] == '\0')) {
        return fallback;
    }

    errno = 0;
    v = strtod(s, &endp);
    if ((errno != 0) || (endp == s) || (*endp != '\0') ||
        (isfinite(v) == 0) || (v < lo) || (v > hi)) {
        return fallback;
    }
    return v;
}

}

mars_status_t store::save(const char *path, const mars_model_t *m)
{
    FILE *fp;

    if ((path == NULL) || (m == NULL)) {
        return MARS_ERR_ARG;
    }

    fp = fopen(path, "wb");
    if (fp == NULL) {
        return MARS_ERR_IO;
    }

    if (fwrite(m, sizeof(*m), 1U, fp) != 1U) {
        (void)fclose(fp);
        return MARS_ERR_IO;
    }

    if (fclose(fp) != 0) {
        return MARS_ERR_IO;
    }

    return MARS_OK;
}

mars_status_t store::load(const char *path, mars_model_t *m)
{
    FILE *fp;

    if ((path == NULL) || (m == NULL)) {
        return MARS_ERR_ARG;
    }

    fp = fopen(path, "rb");
    if (fp == NULL) {
        return MARS_ERR_IO;
    }

    if (fread(m, sizeof(*m), 1U, fp) != 1U) {
        (void)fclose(fp);
        return MARS_ERR_IO;
    }

    if (fclose(fp) != 0) {
        return MARS_ERR_IO;
    }

    if ((m->magic != MARS_MAGIC) || (m->version != MARS_VERSION) ||
        (m->k == 0U) || (m->k > MARS_MAX_STATES) ||
        (m->aug_dim == 0U) || (m->aug_dim > MARS_MAX_AUG_FEATURES) ||
        (m->tick_size <= 0.0)) {
        return MARS_ERR_STATE;
    }

    return MARS_OK;
}


double store::select_score(const mars_bt_stats_t *s)
{
    const double penalty = env_double("MARS_TURNOVER_PENALTY", 1.0e-6, 0.0, 1.0);

    if ((s == NULL) || (s->trades < 5.0)) {
        return -DBL_MAX / 4.0;
    }
    return s->sharpe_bar - (penalty * s->turnover);
}


mars_config_t store::default_config(void)
{
    mars_config_t c;
    c.horizon = env_u32("MARS_HORIZON", MARS_DEFAULT_HORIZON, 1U, 100000U);
    c.tick_size = env_double("MARS_TICK_SIZE", MARS_DEFAULT_TICK_SIZE, 1.0e-9, 1000000.0);
    c.turn_cost_ticks = env_double("MARS_TURN_COST", MARS_DEFAULT_TURN_COST, 0.0, 1000000.0);
    c.edge_cost_ticks = env_double("MARS_EDGE_COST", MARS_DEFAULT_EDGE_COST, 0.0, 1000000.0);
    c.buffer_ticks = env_double("MARS_BUFFER", MARS_DEFAULT_BUFFER, 0.0, 1000000.0);
    c.max_spread_ticks = env_double("MARS_MAX_SPREAD", MARS_DEFAULT_MAX_SPREAD, 0.0, 1000000.0);
    c.pos_max = env_double("MARS_POS_MAX", MARS_DEFAULT_POS_MAX, 0.0, 1000000.0);
    c.risk_lambda = env_double("MARS_RISK_LAMBDA", 0.25, 0.0, 1000000.0);
    return c;
}


mars_status_t store::init_from_config(mars_model_t *m, const mars_config_t *cfg, uint32_t k)
{
    if ((m == NULL) || (cfg == NULL) || (k == 0U) || (k > MARS_MAX_STATES)) {
        return MARS_ERR_ARG;
    }

    memset(m, 0, sizeof(*m));
    m->magic = MARS_MAGIC;
    m->version = MARS_VERSION;
    m->horizon = cfg->horizon;
    m->k = k;
    m->base_dim = MARS_MAX_BASE_FEATURES;
    m->aug_dim = model_aug_dim(MARS_MAX_BASE_FEATURES, k);
    if (m->aug_dim > MARS_MAX_AUG_FEATURES) {
        return MARS_ERR_STATE;
    }
    m->tick_size = cfg->tick_size;
    m->turn_cost_ticks = cfg->turn_cost_ticks;
    m->edge_cost_ticks = cfg->edge_cost_ticks;
    m->buffer_ticks = cfg->buffer_ticks;
    m->max_spread_ticks = cfg->max_spread_ticks;
    m->pos_max = cfg->pos_max;
    m->risk_lambda = cfg->risk_lambda;
    return MARS_OK;
}

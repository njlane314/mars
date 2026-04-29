#include <float.h>
#include <stdio.h>
#include <string.h>

#include "model_store.hpp"

namespace {

static uint32_t model_aug_dim(uint32_t base_dim, uint32_t k)
{
    return base_dim + k + (base_dim * k);
}

}

rt_status_t ModelStore::save(const char *path, const rt_model_t *m)
{
    FILE *fp;

    if ((path == NULL) || (m == NULL)) {
        return RT_ERR_ARG;
    }

    fp = fopen(path, "wb");
    if (fp == NULL) {
        return RT_ERR_IO;
    }

    if (fwrite(m, sizeof(*m), 1U, fp) != 1U) {
        (void)fclose(fp);
        return RT_ERR_IO;
    }

    if (fclose(fp) != 0) {
        return RT_ERR_IO;
    }

    return RT_OK;
}


rt_status_t ModelStore::load(const char *path, rt_model_t *m)
{
    FILE *fp;

    if ((path == NULL) || (m == NULL)) {
        return RT_ERR_ARG;
    }

    fp = fopen(path, "rb");
    if (fp == NULL) {
        return RT_ERR_IO;
    }

    if (fread(m, sizeof(*m), 1U, fp) != 1U) {
        (void)fclose(fp);
        return RT_ERR_IO;
    }

    if (fclose(fp) != 0) {
        return RT_ERR_IO;
    }

    if ((m->magic != RT_MAGIC) || (m->version != RT_VERSION) ||
        (m->k == 0U) || (m->k > RT_MAX_STATES) ||
        (m->aug_dim == 0U) || (m->aug_dim > RT_MAX_AUG_FEATURES) ||
        (m->tick_size <= 0.0)) {
        return RT_ERR_STATE;
    }

    return RT_OK;
}


double ModelStore::selectScore(const rt_bt_stats_t *s)
{
    if ((s == NULL) || (s->trades < 5.0)) {
        return -DBL_MAX / 4.0;
    }
    return s->sharpe_bar - (1.0e-5 * s->turnover);
}


rt_config_t ModelStore::defaultConfig(void)
{
    rt_config_t c;
    c.horizon = RT_DEFAULT_HORIZON;
    c.tick_size = RT_DEFAULT_TICK_SIZE;
    c.turn_cost_ticks = RT_DEFAULT_TURN_COST;
    c.edge_cost_ticks = RT_DEFAULT_EDGE_COST;
    c.buffer_ticks = RT_DEFAULT_BUFFER;
    c.max_spread_ticks = RT_DEFAULT_MAX_SPREAD;
    c.pos_max = RT_DEFAULT_POS_MAX;
    return c;
}


rt_status_t ModelStore::initFromConfig(rt_model_t *m, const rt_config_t *cfg, uint32_t k)
{
    if ((m == NULL) || (cfg == NULL) || (k == 0U) || (k > RT_MAX_STATES)) {
        return RT_ERR_ARG;
    }

    memset(m, 0, sizeof(*m));
    m->magic = RT_MAGIC;
    m->version = RT_VERSION;
    m->horizon = cfg->horizon;
    m->k = k;
    m->base_dim = RT_MAX_BASE_FEATURES;
    m->aug_dim = model_aug_dim(RT_MAX_BASE_FEATURES, k);
    if (m->aug_dim > RT_MAX_AUG_FEATURES) {
        return RT_ERR_STATE;
    }
    m->tick_size = cfg->tick_size;
    m->turn_cost_ticks = cfg->turn_cost_ticks;
    m->edge_cost_ticks = cfg->edge_cost_ticks;
    m->buffer_ticks = cfg->buffer_ticks;
    m->max_spread_ticks = cfg->max_spread_ticks;
    m->pos_max = cfg->pos_max;
    return RT_OK;
}


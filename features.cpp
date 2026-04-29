#include <math.h>
#include <stdlib.h>

#include "features.hpp"

static double dabs(double x)
{
    return (x < 0.0) ? -x : x;
}


static double rolling_sum(const double *x, size_t n, size_t t, size_t w)
{
    size_t i;
    size_t start;
    double s = 0.0;

    if ((x == NULL) || (n == 0U) || (t >= n)) {
        return 0.0;
    }

    start = (t >= w) ? (t + 1U - w) : 0U;
    for (i = start; i <= t; ++i) {
        s += x[i];
    }
    return s;
}


static double rolling_mean(const double *x, size_t n, size_t t, size_t w)
{
    size_t start;
    size_t cnt;

    if ((x == NULL) || (n == 0U) || (t >= n)) {
        return 0.0;
    }

    start = (t >= w) ? (t + 1U - w) : 0U;
    cnt = t + 1U - start;
    return rolling_sum(x, n, t, w) / (double)cnt;
}


static double rolling_std(const double *x, size_t n, size_t t, size_t w)
{
    size_t i;
    size_t start;
    size_t cnt;
    double mu;
    double ss = 0.0;

    if ((x == NULL) || (n == 0U) || (t >= n)) {
        return 0.0;
    }

    start = (t >= w) ? (t + 1U - w) : 0U;
    cnt = t + 1U - start;
    if (cnt <= 1U) {
        return 0.0;
    }

    mu = rolling_mean(x, n, t, w);
    for (i = start; i <= t; ++i) {
        const double z = x[i] - mu;
        ss += z * z;
    }
    return sqrt(ss / (double)(cnt - 1U));
}


static double time_sin(uint64_t ts)
{
    const double two_pi = 6.2831853071795864769;
    const uint64_t sec_day = ts % 86400ULL;
    return sin(two_pi * (double)sec_day / 86400.0);
}


static double time_cos(uint64_t ts)
{
    const double two_pi = 6.2831853071795864769;
    const uint64_t sec_day = ts % 86400ULL;
    return cos(two_pi * (double)sec_day / 86400.0);
}


rt_status_t FeatureBuilder::make(rt_data_t *d, const rt_config_t *cfg)
{
    double *ret1 = NULL;
    double *volume_log = NULL;
    double *depth_log = NULL;
    double *ofi1 = NULL;
    size_t t;
    rt_status_t st = RT_OK;

    if ((d == NULL) || (d->row == NULL) || (cfg == NULL) || (cfg->tick_size <= 0.0)) {
        return RT_ERR_ARG;
    }

    ret1 = (double *)calloc(d->n, sizeof(double));
    volume_log = (double *)calloc(d->n, sizeof(double));
    depth_log = (double *)calloc(d->n, sizeof(double));
    ofi1 = (double *)calloc(d->n, sizeof(double));
    if ((ret1 == NULL) || (volume_log == NULL) || (depth_log == NULL) || (ofi1 == NULL)) {
        st = RT_ERR_MEM;
        goto done;
    }

    for (t = 0U; t < d->n; ++t) {
        rt_row_t *r = &d->row[t];
        const double denom = r->bid_sz + r->ask_sz + RT_EPS;
        const double micro = ((r->ask * r->bid_sz) + (r->bid * r->ask_sz)) / denom;

        r->mid = 0.5 * (r->bid + r->ask);
        r->log_mid = log(r->mid);
        r->spread_ticks = (r->ask - r->bid) / cfg->tick_size;
        r->depth = r->bid_sz + r->ask_sz;
        r->imbalance = (r->bid_sz - r->ask_sz) / denom;
        r->micro_dev_ticks = (micro - r->mid) / cfg->tick_size;

        if (t == 0U) {
            r->ret1_ticks = 0.0;
            ofi1[t] = 0.0;
        } else {
            const rt_row_t *p = &d->row[t - 1U];
            double e_bid;
            double e_ask;

            r->ret1_ticks = (r->mid - p->mid) / cfg->tick_size;

            e_bid = 0.0;
            if (r->bid >= p->bid) {
                e_bid += r->bid_sz;
            }
            if (r->bid <= p->bid) {
                e_bid -= p->bid_sz;
            }

            e_ask = 0.0;
            if (r->ask <= p->ask) {
                e_ask -= r->ask_sz;
            }
            if (r->ask >= p->ask) {
                e_ask += p->ask_sz;
            }

            ofi1[t] = (e_bid + e_ask) / (0.5 * (r->depth + p->depth) + RT_EPS);
        }

        ret1[t] = r->ret1_ticks;
        volume_log[t] = log(1.0 + r->volume);
        depth_log[t] = log(1.0 + r->depth);
    }

    for (t = 0U; t < d->n; ++t) {
        rt_row_t *r = &d->row[t];
        const double ret5 = (t >= 5U) ? ((r->mid - d->row[t - 5U].mid) / cfg->tick_size) : 0.0;
        const double ret10 = (t >= 10U) ? ((r->mid - d->row[t - 10U].mid) / cfg->tick_size) : 0.0;
        const double ret20 = (t >= 20U) ? ((r->mid - d->row[t - 20U].mid) / cfg->tick_size) : 0.0;
        const double rv30 = rolling_std(ret1, d->n, t, 30U) * sqrt(30.0);
        const double rv60 = rolling_std(ret1, d->n, t, 60U) * sqrt(60.0);
        const double ofi5 = rolling_sum(ofi1, d->n, t, 5U);
        const double ofi20 = rolling_sum(ofi1, d->n, t, 20U);
        const double vol_mu = rolling_mean(volume_log, d->n, t, 120U);
        const double vol_sd = rolling_std(volume_log, d->n, t, 120U);
        const double dep_mu = rolling_mean(depth_log, d->n, t, 120U);
        const double dep_sd = rolling_std(depth_log, d->n, t, 120U);
        const double vol_z = (volume_log[t] - vol_mu) / (vol_sd + RT_EPS);
        const double dep_z = (depth_log[t] - dep_mu) / (dep_sd + RT_EPS);
        double vwap20 = r->mid;
        double num = 0.0;
        double den = 0.0;
        size_t j;
        const size_t start = (t >= 20U) ? (t + 1U - 20U) : 0U;

        for (j = start; j <= t; ++j) {
            num += d->row[j].mid * d->row[j].volume;
            den += d->row[j].volume;
        }
        if (den > RT_EPS) {
            vwap20 = num / den;
        }

        r->ofi1_norm = ofi1[t];
        r->ofi5_norm = ofi5;

        r->reg[0] = r->ret1_ticks;
        r->reg[1] = ret5;
        r->reg[2] = dabs(ret5);
        r->reg[3] = rv30;
        r->reg[4] = r->spread_ticks;
        r->reg[5] = depth_log[t];
        r->reg[6] = r->imbalance;
        r->reg[7] = ofi5;

        r->base[0] = r->micro_dev_ticks;
        r->base[1] = r->imbalance;
        r->base[2] = ofi1[t];
        r->base[3] = ofi5;
        r->base[4] = ofi20;
        r->base[5] = r->ret1_ticks;
        r->base[6] = ret5;
        r->base[7] = ret10;
        r->base[8] = ret20;
        r->base[9] = (r->mid - vwap20) / cfg->tick_size;
        r->base[10] = rv30;
        r->base[11] = rv60;
        r->base[12] = vol_z + dep_z;
        r->base[13] = r->spread_ticks;
        r->base[14] = time_sin(r->ts);
        r->base[15] = time_cos(r->ts);

        if (cfg->horizon + t + 1U < d->n) {
            r->label_ticks = (d->row[t + cfg->horizon + 1U].mid -
                              d->row[t + 1U].mid) / cfg->tick_size;
        } else {
            r->label_ticks = 0.0;
        }

    }

done:
    free(ret1);
    free(volume_log);
    free(depth_log);
    free(ofi1);
    return st;
}

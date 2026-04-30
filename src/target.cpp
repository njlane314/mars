#include <math.h>
#include <stdlib.h>

#include "target.hpp"

namespace {

static double dmax(double a, double b)
{
    return (a > b) ? a : b;
}

static int residual_btc(const mars_data_t *d)
{
    return ((d != NULL) && ((d->flags & MARS_DATA_RESIDUAL_BTC) != 0U)) ? 1 : 0;
}

static double safe_log_ret(double now, double prev)
{
    if ((now <= 0.0) || (prev <= 0.0) ||
        (isfinite(now) == 0) || (isfinite(prev) == 0)) {
        return 0.0;
    }
    return log(now / prev);
}

static double rolling_beta(const double *asset, const double *factor, size_t n, size_t t, size_t w)
{
    size_t start;
    size_t i;
    size_t cnt = 0U;
    double ma = 0.0;
    double mf = 0.0;
    double cov = 0.0;
    double var = 0.0;

    if ((asset == NULL) || (factor == NULL) || (n == 0U) || (t >= n)) {
        return 0.0;
    }

    start = (t > w) ? (t - w) : 1U;
    if (start > t) {
        return 0.0;
    }

    for (i = start; i <= t; ++i) {
        ma += asset[i];
        mf += factor[i];
        ++cnt;
    }
    if (cnt < 20U) {
        return 0.0;
    }
    ma /= (double)cnt;
    mf /= (double)cnt;

    for (i = start; i <= t; ++i) {
        const double a = asset[i] - ma;
        const double f = factor[i] - mf;
        cov += a * f;
        var += f * f;
    }

    return cov / (var + MARS_EPS);
}

static double rolling_std(const double *x, size_t n, size_t t, size_t w)
{
    size_t start;
    size_t i;
    size_t cnt = 0U;
    double mu = 0.0;
    double ss = 0.0;

    if ((x == NULL) || (n == 0U) || (t >= n)) {
        return 0.0;
    }

    start = (t > w) ? (t - w) : 1U;
    if (start > t) {
        return 0.0;
    }
    for (i = start; i <= t; ++i) {
        mu += x[i];
        ++cnt;
    }
    if (cnt < 2U) {
        return 0.0;
    }
    mu /= (double)cnt;
    for (i = start; i <= t; ++i) {
        const double z = x[i] - mu;
        ss += z * z;
    }
    return sqrt(ss / dmax(1.0, (double)cnt - 1.0));
}

}

mars_status_t target::make(mars_data_t *d, const mars_config_t *cfg)
{
    double *asset_ret = NULL;
    double *factor_ret = NULL;
    double *step_ticks = NULL;
    size_t t;
    const int use_residual = residual_btc(d);

    if ((d == NULL) || (d->row == NULL) || (cfg == NULL) || (cfg->tick_size <= 0.0)) {
        return MARS_ERR_ARG;
    }

    asset_ret = (double *)calloc(d->n, sizeof(double));
    factor_ret = (double *)calloc(d->n, sizeof(double));
    step_ticks = (double *)calloc(d->n, sizeof(double));
    if ((asset_ret == NULL) || (factor_ret == NULL) || (step_ticks == NULL)) {
        free(asset_ret);
        free(factor_ret);
        free(step_ticks);
        return MARS_ERR_MEM;
    }

    for (t = 0U; t < d->n; ++t) {
        mars_row_t *r = &d->row[t];

        r->factor_mid = use_residual ? r->aux[MARS_AUX_FACTOR_PRICE] : 0.0;
        r->factor_beta = 0.0;
        r->risk_ticks = 0.0;
        r->trade_ret_ticks = r->ret1_ticks;
        r->label_ticks = 0.0;

        if (t > 0U) {
            asset_ret[t] = safe_log_ret(r->mid, d->row[t - 1U].mid);
            if (use_residual != 0) {
                factor_ret[t] = safe_log_ret(r->factor_mid, d->row[t - 1U].factor_mid);
            }
        }
    }

    for (t = 0U; t < d->n; ++t) {
        mars_row_t *r = &d->row[t];
        const double beta = (use_residual != 0) ? rolling_beta(asset_ret, factor_ret, d->n, t, 168U) : 0.0;
        const double resid_ret = asset_ret[t] - (beta * factor_ret[t]);

        r->factor_beta = beta;
        step_ticks[t] = (t > 0U) ? (resid_ret * d->row[t - 1U].mid / cfg->tick_size) : 0.0;
        r->trade_ret_ticks = (use_residual != 0) ? step_ticks[t] : r->ret1_ticks;
        r->risk_ticks = rolling_std(step_ticks, d->n, t, 168U) * sqrt((double)cfg->horizon);

        if (cfg->horizon + t + 1U < d->n) {
            if (use_residual != 0) {
                const size_t a = t + 1U;
                const size_t b = t + cfg->horizon + 1U;
                const double asset = safe_log_ret(d->row[b].mid, d->row[a].mid);
                const double factor = safe_log_ret(d->row[b].factor_mid, d->row[a].factor_mid);
                r->label_ticks = (asset - (beta * factor)) * d->row[a].mid / cfg->tick_size;
            } else {
                r->label_ticks = (d->row[t + cfg->horizon + 1U].mid -
                                  d->row[t + 1U].mid) / cfg->tick_size;
            }
        }
    }

    free(asset_ret);
    free(factor_ret);
    free(step_ticks);
    return MARS_OK;
}

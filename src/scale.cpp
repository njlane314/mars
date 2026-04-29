#include <math.h>
#include <string.h>

#include "scale.hpp"

static double dmax(double a, double b)
{
    return (a > b) ? a : b;
}


static double clip(double x, double lo, double hi)
{
    if (x < lo) {
        return lo;
    }
    if (x > hi) {
        return hi;
    }
    return x;
}


mars_status_t scale::fit(mars_scaler_t *s, const double *x, size_t n, uint32_t d)
{
    size_t i;
    uint32_t j;

    if ((s == NULL) || (x == NULL) || (n == 0U) || (d == 0U) || (d > MARS_MAX_AUG_FEATURES)) {
        return MARS_ERR_ARG;
    }

    memset(s, 0, sizeof(*s));
    s->d = d;

    for (j = 0U; j < d; ++j) {
        double sum = 0.0;
        for (i = 0U; i < n; ++i) {
            sum += x[(i * d) + j];
        }
        s->mean[j] = sum / (double)n;
    }

    for (j = 0U; j < d; ++j) {
        double ss = 0.0;
        for (i = 0U; i < n; ++i) {
            const double z = x[(i * d) + j] - s->mean[j];
            ss += z * z;
        }
        s->sd[j] = sqrt(ss / dmax(1.0, (double)n - 1.0));
        if ((s->sd[j] < 1.0e-8) || (!isfinite(s->sd[j]))) {
            s->sd[j] = 1.0;
        }
    }

    return MARS_OK;
}


void scale::apply_vec(const mars_scaler_t *s, const double *x, double *z)
{
    uint32_t j;

    for (j = 0U; j < s->d; ++j) {
        z[j] = (x[j] - s->mean[j]) / (s->sd[j] + MARS_EPS);
        z[j] = clip(z[j], -12.0, 12.0);
    }
}


void scale::rows_to_matrix_reg(const mars_data_t *d, size_t start, size_t end, double *x)
{
    size_t i;
    uint32_t j;

    for (i = start; i < end; ++i) {
        for (j = 0U; j < MARS_MAX_REG_FEATURES; ++j) {
            x[((i - start) * MARS_MAX_REG_FEATURES) + j] = d->row[i].reg[j];
        }
    }
}


void scale::rows_to_matrix_base(const mars_data_t *d, size_t start, size_t end, double *x)
{
    size_t i;
    uint32_t j;

    for (i = start; i < end; ++i) {
        for (j = 0U; j < MARS_MAX_BASE_FEATURES; ++j) {
            x[((i - start) * MARS_MAX_BASE_FEATURES) + j] = d->row[i].base[j];
        }
    }
}

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "alpha.hpp"
#include "backtest.hpp"
#include "hmm.hpp"

#include "scale.hpp"

static double dabs(double x)
{
    return (x < 0.0) ? -x : x;
}


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


static int is_finite(double x)
{
    return isfinite(x) ? 1 : 0;
}


static double soft_threshold(double x, double lambda)
{
    if (x > lambda) {
        return x - lambda;
    }
    if (x < -lambda) {
        return x + lambda;
    }
    return 0.0;
}

uint32_t alpha::aug_dim(uint32_t base_dim, uint32_t k)
{
    return base_dim + k + (base_dim * k);
}


static void build_aug_raw(
    const mars_row_t *r,
    const mars_scaler_t *base_scaler,
    const double *pi,
    uint32_t k,
    double *aug)
{
    double base[MARS_MAX_BASE_FEATURES];
    uint32_t j;
    uint32_t q;
    uint32_t p = 0U;

    scale::apply_vec(base_scaler, r->base, base);

    for (j = 0U; j < MARS_MAX_BASE_FEATURES; ++j) {
        aug[p] = base[j];
        ++p;
    }
    for (q = 0U; q < k; ++q) {
        aug[p] = pi[q];
        ++p;
    }
    for (q = 0U; q < k; ++q) {
        for (j = 0U; j < MARS_MAX_BASE_FEATURES; ++j) {
            aug[p] = pi[q] * base[j];
            ++p;
        }
    }
}


static mars_status_t fit_base_scaler(mars_model_t *m, const mars_data_t *d, size_t start, size_t end)
{
    double *raw = NULL;
    size_t n;
    mars_status_t st;

    if ((m == NULL) || (d == NULL) || (end <= start)) {
        return MARS_ERR_ARG;
    }

    n = end - start;
    raw = (double *)calloc(n * MARS_MAX_BASE_FEATURES, sizeof(double));
    if (raw == NULL) {
        return MARS_ERR_MEM;
    }

    scale::rows_to_matrix_base(d, start, end, raw);
    st = scale::fit(&m->base_scaler, raw, n, MARS_MAX_BASE_FEATURES);
    free(raw);
    return st;
}


static mars_status_t build_aug_matrix(
    const mars_model_t *m,
    const mars_data_t *d,
    size_t start,
    size_t end,
    const double *pi,
    double *x_aug_raw)
{
    size_t i;
    uint32_t augd;

    if ((m == NULL) || (d == NULL) || (pi == NULL) || (x_aug_raw == NULL) || (end <= start)) {
        return MARS_ERR_ARG;
    }

    augd = m->aug_dim;
    for (i = start; i < end; ++i) {
        build_aug_raw(&d->row[i], &m->base_scaler, &pi[(i - start) * m->k], m->k,
                         &x_aug_raw[(i - start) * augd]);
    }

    return MARS_OK;
}


static void apply_aug_scaler_matrix(const mars_scaler_t *s, double *x, size_t n)
{
    size_t i;
    uint32_t j;

    for (i = 0U; i < n; ++i) {
        for (j = 0U; j < s->d; ++j) {
            double z = (x[(i * s->d) + j] - s->mean[j]) / (s->sd[j] + MARS_EPS);
            x[(i * s->d) + j] = clip(z, -12.0, 12.0);
        }
    }
}


static double predict_row(const mars_model_t *m, const mars_row_t *r, const double *pi)
{
    double aug_raw[MARS_MAX_AUG_FEATURES];
    double aug[MARS_MAX_AUG_FEATURES];
    double y = m->alpha_intercept;
    uint32_t j;

    build_aug_raw(r, &m->base_scaler, pi, m->k, aug_raw);
    scale::apply_vec(&m->aug_scaler, aug_raw, aug);

    for (j = 0U; j < m->aug_dim; ++j) {
        y += m->beta[j] * aug[j];
    }
    return y;
}


static mars_status_t enet_fit(
    const double *x,
    const double *y,
    size_t n,
    uint32_t d,
    double lambda,
    double l1_ratio,
    double *intercept,
    double *beta)
{
    double *res = NULL;
    double mean_y = 0.0;
    size_t i;
    uint32_t j;
    uint32_t iter;

    if ((x == NULL) || (y == NULL) || (intercept == NULL) || (beta == NULL) ||
        (n == 0U) || (d == 0U) || (d > MARS_MAX_AUG_FEATURES)) {
        return MARS_ERR_ARG;
    }

    res = (double *)calloc(n, sizeof(double));
    if (res == NULL) {
        return MARS_ERR_MEM;
    }

    for (i = 0U; i < n; ++i) {
        mean_y += y[i];
    }
    mean_y /= (double)n;
    *intercept = mean_y;

    for (j = 0U; j < d; ++j) {
        beta[j] = 0.0;
    }
    for (i = 0U; i < n; ++i) {
        res[i] = y[i] - mean_y;
    }

    for (iter = 0U; iter < MARS_MAX_CD_ITERS; ++iter) {
        double max_delta = 0.0;
        for (j = 0U; j < d; ++j) {
            double rho = 0.0;
            double z = 0.0;
            double oldb = beta[j];
            double newb;

            for (i = 0U; i < n; ++i) {
                const double xij = x[(i * d) + j];
                res[i] += xij * oldb;
                rho += xij * res[i];
                z += xij * xij;
            }
            rho /= (double)n;
            z /= (double)n;

            newb = soft_threshold(rho, lambda * l1_ratio) /
                   (z + lambda * (1.0 - l1_ratio) + MARS_EPS);

            for (i = 0U; i < n; ++i) {
                const double xij = x[(i * d) + j];
                res[i] -= xij * newb;
            }

            beta[j] = newb;
            max_delta = dmax(max_delta, dabs(newb - oldb));
        }

        if (max_delta < 1.0e-8) {
            break;
        }
    }

    free(res);
    return MARS_OK;
}


mars_status_t alpha::predict_range(
    const mars_model_t *m,
    const mars_data_t *d,
    size_t start,
    size_t end,
    double *pred)
{
    double *pi = NULL;
    size_t i;
    mars_status_t st;

    if ((m == NULL) || (d == NULL) || (pred == NULL) || (end <= start)) {
        return MARS_ERR_ARG;
    }

    pi = (double *)calloc((end - start) * m->k, sizeof(double));
    if (pi == NULL) {
        return MARS_ERR_MEM;
    }

    st = hmm::filter_range(&m->hmm, d, start, end, pi);
    if (st != MARS_OK) {
        free(pi);
        return st;
    }

    for (i = start; i < end; ++i) {
        pred[i - start] = predict_row(m, &d->row[i], &pi[(i - start) * m->k]);
        if (!is_finite(pred[i - start])) {
            free(pi);
            return MARS_ERR_NUM;
        }
    }

    free(pi);
    return MARS_OK;
}


mars_status_t alpha::train_eval(
    mars_model_t *m,
    const mars_data_t *d,
    size_t train_start,
    size_t train_end,
    size_t val_start,
    size_t val_end,
    double lambda,
    mars_bt_stats_t *stats_out)
{
    double *pi_train = NULL;
    double *pi_val = NULL;
    double *x_train = NULL;
    double *x_val = NULL;
    double *y_train = NULL;
    double *pred = NULL;
    mars_status_t st = MARS_OK;
    size_t n_train;
    size_t n_val;
    size_t i;

    if ((m == NULL) || (d == NULL) || (stats_out == NULL) ||
        (train_end <= train_start) || (val_end <= val_start)) {
        return MARS_ERR_ARG;
    }

    n_train = train_end - train_start;
    n_val = val_end - val_start;

    pi_train = (double *)calloc(n_train * m->k, sizeof(double));
    pi_val = (double *)calloc(n_val * m->k, sizeof(double));
    x_train = (double *)calloc(n_train * m->aug_dim, sizeof(double));
    x_val = (double *)calloc(n_val * m->aug_dim, sizeof(double));
    y_train = (double *)calloc(n_train, sizeof(double));
    pred = (double *)calloc(n_val, sizeof(double));
    if ((pi_train == NULL) || (pi_val == NULL) || (x_train == NULL) ||
        (x_val == NULL) || (y_train == NULL) || (pred == NULL)) {
        st = MARS_ERR_MEM;
        goto done;
    }

    st = fit_base_scaler(m, d, train_start, train_end);
    if (st != MARS_OK) {
        goto done;
    }

    st = hmm::filter_range(&m->hmm, d, train_start, train_end, pi_train);
    if (st != MARS_OK) {
        goto done;
    }
    st = hmm::filter_range(&m->hmm, d, val_start, val_end, pi_val);
    if (st != MARS_OK) {
        goto done;
    }

    st = build_aug_matrix(m, d, train_start, train_end, pi_train, x_train);
    if (st != MARS_OK) {
        goto done;
    }
    st = scale::fit(&m->aug_scaler, x_train, n_train, m->aug_dim);
    if (st != MARS_OK) {
        goto done;
    }
    apply_aug_scaler_matrix(&m->aug_scaler, x_train, n_train);

    for (i = 0U; i < n_train; ++i) {
        y_train[i] = d->row[train_start + i].label_ticks;
    }

    st = enet_fit(x_train, y_train, n_train, m->aug_dim, lambda, 0.80, &m->alpha_intercept, m->beta);
    if (st != MARS_OK) {
        goto done;
    }

    st = build_aug_matrix(m, d, val_start, val_end, pi_val, x_val);
    if (st != MARS_OK) {
        goto done;
    }
    apply_aug_scaler_matrix(&m->aug_scaler, x_val, n_val);

    for (i = 0U; i < n_val; ++i) {
        uint32_t j;
        double yhat = m->alpha_intercept;
        for (j = 0U; j < m->aug_dim; ++j) {
            yhat += m->beta[j] * x_val[(i * m->aug_dim) + j];
        }
        pred[i] = yhat;
    }

    *stats_out = backtest::evaluate(m, d, val_start, val_end, pred, NULL);

done:
    free(pi_train);
    free(pi_val);
    free(x_train);
    free(x_val);
    free(y_train);
    free(pred);
    return st;
}


mars_status_t alpha::train_final(
    mars_model_t *m,
    const mars_data_t *d,
    size_t train_start,
    size_t train_end,
    double lambda)
{
    mars_bt_stats_t dummy;
    return alpha::train_eval(m, d, train_start, train_end, train_start, train_end, lambda, &dummy);
}

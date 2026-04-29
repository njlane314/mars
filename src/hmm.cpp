#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "hmm.hpp"

#include "scale.hpp"

static double dmax(double a, double b)
{
    return (a > b) ? a : b;
}


static double logaddexp_d(double a, double b)
{
    double m;

    if (a <= MARS_LOG_ZERO) {
        return b;
    }
    if (b <= MARS_LOG_ZERO) {
        return a;
    }

    m = (a > b) ? a : b;
    return m + log(exp(a - m) + exp(b - m));
}


static double logsumexp_d(const double *x, uint32_t n)
{
    double m = MARS_LOG_ZERO;
    double s = 0.0;
    uint32_t i;

    for (i = 0U; i < n; ++i) {
        if (x[i] > m) {
            m = x[i];
        }
    }

    if (m <= MARS_LOG_ZERO) {
        return MARS_LOG_ZERO;
    }

    for (i = 0U; i < n; ++i) {
        s += exp(x[i] - m);
    }

    return m + log(s + MARS_EPS);
}

static double hmm_emit_logp(const mars_hmm_t *h, uint32_t k, const double *x)
{
    double lp = 0.0;
    uint32_t j;

    for (j = 0U; j < h->d; ++j) {
        const double v = dmax(h->var[k][j], MARS_VAR_FLOOR);
        const double e = x[j] - h->mu[k][j];
        lp += -0.5 * ((e * e) / v + log(6.2831853071795864769 * v));
    }
    return lp;
}


static void hmm_normalize_row(double *row, uint32_t n)
{
    uint32_t i;
    double s = 0.0;

    for (i = 0U; i < n; ++i) {
        if (!isfinite(row[i]) || (row[i] < 0.0)) {
            row[i] = 0.0;
        }
        s += row[i];
    }

    if (s <= MARS_EPS) {
        for (i = 0U; i < n; ++i) {
            row[i] = 1.0 / (double)n;
        }
    } else {
        for (i = 0U; i < n; ++i) {
            row[i] /= s;
        }
    }
}


static void hmm_init(mars_hmm_t *h, uint32_t k, uint32_t d, const double *x, size_t n)
{
    size_t i;
    uint32_t a;
    uint32_t b;
    double global_mean[MARS_MAX_REG_FEATURES];
    double global_sd[MARS_MAX_REG_FEATURES];

    memset(h, 0, sizeof(*h));
    h->k = k;
    h->d = d;

    for (a = 0U; a < k; ++a) {
        h->pi[a] = 1.0 / (double)k;
        for (b = 0U; b < k; ++b) {
            h->a[a][b] = (a == b) ? 0.90 : (0.10 / (double)(k - 1U));
        }
        if (k == 1U) {
            h->a[a][a] = 1.0;
        }
    }

    for (b = 0U; b < d; ++b) {
        double sum = 0.0;
        double ss = 0.0;
        for (i = 0U; i < n; ++i) {
            sum += x[(i * d) + b];
        }
        global_mean[b] = sum / (double)n;
        for (i = 0U; i < n; ++i) {
            const double z = x[(i * d) + b] - global_mean[b];
            ss += z * z;
        }
        global_sd[b] = sqrt(ss / dmax(1.0, (double)n - 1.0));
        if (global_sd[b] < 1.0e-6) {
            global_sd[b] = 1.0;
        }
    }

    for (a = 0U; a < k; ++a) {
        const double shift = ((double)a - 0.5 * ((double)k - 1.0)) / dmax(1.0, (double)k - 1.0);
        for (b = 0U; b < d; ++b) {
            h->mu[a][b] = global_mean[b];
            h->var[a][b] = dmax(global_sd[b] * global_sd[b], MARS_VAR_FLOOR);
        }
        /* Put initial states along a volatility/liquidity axis. */
        h->mu[a][2] += 0.75 * shift; /* |ret5| */
        h->mu[a][3] += 0.75 * shift; /* rv30 */
        h->mu[a][4] += 0.50 * shift; /* spread */
    }
}


static mars_status_t hmm_forward_scaled(
    const mars_hmm_t *h,
    const double *x,
    size_t n,
    double *alpha,
    double *scale,
    double *loglike_out)
{
    size_t t;
    uint32_t i;
    uint32_t j;
    double loge[MARS_MAX_STATES];
    double e[MARS_MAX_STATES];
    double maxloge;
    double ll = 0.0;

    if ((h == NULL) || (x == NULL) || (alpha == NULL) || (scale == NULL) || (loglike_out == NULL)) {
        return MARS_ERR_ARG;
    }

    for (t = 0U; t < n; ++t) {
        maxloge = MARS_LOG_ZERO;
        for (j = 0U; j < h->k; ++j) {
            loge[j] = hmm_emit_logp(h, j, &x[t * h->d]);
            if (loge[j] > maxloge) {
                maxloge = loge[j];
            }
        }
        for (j = 0U; j < h->k; ++j) {
            e[j] = exp(loge[j] - maxloge);
        }

        if (t == 0U) {
            double s = 0.0;
            for (j = 0U; j < h->k; ++j) {
                alpha[j] = h->pi[j] * e[j];
                s += alpha[j];
            }
            if (s <= MARS_EPS) {
                return MARS_ERR_NUM;
            }
            scale[0] = s;
            for (j = 0U; j < h->k; ++j) {
                alpha[j] /= s;
            }
            ll += log(s + MARS_EPS) + maxloge;
        } else {
            double s = 0.0;
            for (j = 0U; j < h->k; ++j) {
                double pred = 0.0;
                for (i = 0U; i < h->k; ++i) {
                    pred += alpha[((t - 1U) * h->k) + i] * h->a[i][j];
                }
                alpha[(t * h->k) + j] = pred * e[j];
                s += alpha[(t * h->k) + j];
            }
            if (s <= MARS_EPS) {
                return MARS_ERR_NUM;
            }
            scale[t] = s;
            for (j = 0U; j < h->k; ++j) {
                alpha[(t * h->k) + j] /= s;
            }
            ll += log(s + MARS_EPS) + maxloge;
        }
    }

    *loglike_out = ll;
    return MARS_OK;
}


static mars_status_t hmm_backward_scaled(
    const mars_hmm_t *h,
    const double *x,
    size_t n,
    const double *scale,
    double *beta)
{
    int64_t tt;
    uint32_t i;
    uint32_t j;

    if ((h == NULL) || (x == NULL) || (scale == NULL) || (beta == NULL) || (n == 0U)) {
        return MARS_ERR_ARG;
    }

    for (i = 0U; i < h->k; ++i) {
        beta[((n - 1U) * h->k) + i] = 1.0;
    }

    for (tt = (int64_t)n - 2; tt >= 0; --tt) {
        const size_t t = (size_t)tt;
        double loge[MARS_MAX_STATES];
        double e[MARS_MAX_STATES];
        double maxloge = MARS_LOG_ZERO;

        for (j = 0U; j < h->k; ++j) {
            loge[j] = hmm_emit_logp(h, j, &x[(t + 1U) * h->d]);
            if (loge[j] > maxloge) {
                maxloge = loge[j];
            }
        }
        for (j = 0U; j < h->k; ++j) {
            e[j] = exp(loge[j] - maxloge);
        }

        for (i = 0U; i < h->k; ++i) {
            double s = 0.0;
            for (j = 0U; j < h->k; ++j) {
                s += h->a[i][j] * e[j] * beta[((t + 1U) * h->k) + j];
            }
            beta[(t * h->k) + i] = s / (scale[t + 1U] + MARS_EPS);
        }
    }

    return MARS_OK;
}


static mars_status_t hmm_fit_on_scaled(mars_hmm_t *h, const double *x, size_t n, uint32_t k, uint32_t d)
{
    double *alpha = NULL;
    double *beta = NULL;
    double *scale = NULL;
    double prev_ll = -DBL_MAX;
    mars_status_t st = MARS_OK;
    uint32_t iter;

    if ((h == NULL) || (x == NULL) || (n < MARS_MIN_TRAIN_ROWS) || (k == 0U) || (k > MARS_MAX_STATES) ||
        (d == 0U) || (d > MARS_MAX_REG_FEATURES)) {
        return MARS_ERR_ARG;
    }

    alpha = (double *)calloc(n * k, sizeof(double));
    beta = (double *)calloc(n * k, sizeof(double));
    scale = (double *)calloc(n, sizeof(double));
    if ((alpha == NULL) || (beta == NULL) || (scale == NULL)) {
        st = MARS_ERR_MEM;
        goto done;
    }

    hmm_init(h, k, d, x, n);

    for (iter = 0U; iter < MARS_MAX_EM_ITERS; ++iter) {
        double ll = 0.0;
        double gamma_sum[MARS_MAX_STATES];
        double gamma_init[MARS_MAX_STATES];
        double xi_sum[MARS_MAX_STATES][MARS_MAX_STATES];
        double mu_num[MARS_MAX_STATES][MARS_MAX_REG_FEATURES];
        double var_num[MARS_MAX_STATES][MARS_MAX_REG_FEATURES];
        size_t t;
        uint32_t i;
        uint32_t j;
        uint32_t q;

        st = hmm_forward_scaled(h, x, n, alpha, scale, &ll);
        if (st != MARS_OK) {
            goto done;
        }
        st = hmm_backward_scaled(h, x, n, scale, beta);
        if (st != MARS_OK) {
            goto done;
        }

        memset(gamma_sum, 0, sizeof(gamma_sum));
        memset(gamma_init, 0, sizeof(gamma_init));
        memset(xi_sum, 0, sizeof(xi_sum));
        memset(mu_num, 0, sizeof(mu_num));
        memset(var_num, 0, sizeof(var_num));

        for (t = 0U; t < n; ++t) {
            double g[MARS_MAX_STATES];
            double gs = 0.0;
            for (i = 0U; i < k; ++i) {
                g[i] = alpha[(t * k) + i] * beta[(t * k) + i];
                gs += g[i];
            }
            if (gs <= MARS_EPS) {
                for (i = 0U; i < k; ++i) {
                    g[i] = 1.0 / (double)k;
                }
            } else {
                for (i = 0U; i < k; ++i) {
                    g[i] /= gs;
                }
            }

            if (t == 0U) {
                for (i = 0U; i < k; ++i) {
                    gamma_init[i] = g[i];
                }
            }

            for (i = 0U; i < k; ++i) {
                gamma_sum[i] += g[i];
                for (q = 0U; q < d; ++q) {
                    mu_num[i][q] += g[i] * x[(t * d) + q];
                }
            }
        }

        for (t = 0U; t + 1U < n; ++t) {
            double loge[MARS_MAX_STATES];
            double e[MARS_MAX_STATES];
            double maxloge = MARS_LOG_ZERO;
            double denom = 0.0;

            for (j = 0U; j < k; ++j) {
                loge[j] = hmm_emit_logp(h, j, &x[(t + 1U) * d]);
                if (loge[j] > maxloge) {
                    maxloge = loge[j];
                }
            }
            for (j = 0U; j < k; ++j) {
                e[j] = exp(loge[j] - maxloge);
            }

            for (i = 0U; i < k; ++i) {
                for (j = 0U; j < k; ++j) {
                    denom += alpha[(t * k) + i] * h->a[i][j] * e[j] * beta[((t + 1U) * k) + j];
                }
            }

            if (denom <= MARS_EPS) {
                denom = MARS_EPS;
            }

            for (i = 0U; i < k; ++i) {
                for (j = 0U; j < k; ++j) {
                    const double val = alpha[(t * k) + i] * h->a[i][j] * e[j] *
                                       beta[((t + 1U) * k) + j] / denom;
                    xi_sum[i][j] += val;
                }
            }
        }

        for (i = 0U; i < k; ++i) {
            h->pi[i] = gamma_init[i];
        }
        hmm_normalize_row(h->pi, k);

        for (i = 0U; i < k; ++i) {
            for (j = 0U; j < k; ++j) {
                h->a[i][j] = xi_sum[i][j] / (dmax(gamma_sum[i] - 1.0, MARS_EPS));
                h->a[i][j] = dmax(h->a[i][j], 1.0e-6);
            }
            hmm_normalize_row(h->a[i], k);
        }

        for (i = 0U; i < k; ++i) {
            const double den = dmax(gamma_sum[i], MARS_EPS);
            for (q = 0U; q < d; ++q) {
                h->mu[i][q] = mu_num[i][q] / den;
            }
        }

        for (t = 0U; t < n; ++t) {
            double g[MARS_MAX_STATES];
            double gs = 0.0;
            for (i = 0U; i < k; ++i) {
                g[i] = alpha[(t * k) + i] * beta[(t * k) + i];
                gs += g[i];
            }
            if (gs <= MARS_EPS) {
                gs = 1.0;
            }
            for (i = 0U; i < k; ++i) {
                g[i] /= gs;
                for (q = 0U; q < d; ++q) {
                    const double e2 = x[(t * d) + q] - h->mu[i][q];
                    var_num[i][q] += g[i] * e2 * e2;
                }
            }
        }

        for (i = 0U; i < k; ++i) {
            const double den = dmax(gamma_sum[i], MARS_EPS);
            for (q = 0U; q < d; ++q) {
                h->var[i][q] = dmax(var_num[i][q] / den, MARS_VAR_FLOOR);
            }
        }

        if ((iter > 2U) && (fabs(ll - prev_ll) < 1.0e-5 * dmax(1.0, fabs(prev_ll)))) {
            break;
        }
        prev_ll = ll;
    }

done:
    free(alpha);
    free(beta);
    free(scale);
    return st;
}


mars_status_t HmmModel::fitPreserveScaler(mars_hmm_t *h, const mars_data_t *d, size_t start, size_t end, uint32_t k)
{
    double *raw = NULL;
    double *z = NULL;
    mars_scaler_t scaler;
    mars_status_t st;
    size_t n;
    size_t i;

    if ((h == NULL) || (d == NULL) || (d->row == NULL) || (end <= start)) {
        return MARS_ERR_ARG;
    }

    n = end - start;
    raw = (double *)calloc(n * MARS_MAX_REG_FEATURES, sizeof(double));
    z = (double *)calloc(n * MARS_MAX_REG_FEATURES, sizeof(double));
    if ((raw == NULL) || (z == NULL)) {
        st = MARS_ERR_MEM;
        goto done;
    }

    ScalerOps::rowsToMatrixReg(d, start, end, raw);
    st = ScalerOps::fit(&scaler, raw, n, MARS_MAX_REG_FEATURES);
    if (st != MARS_OK) {
        goto done;
    }

    for (i = 0U; i < n; ++i) {
        ScalerOps::applyVec(&scaler, &raw[i * MARS_MAX_REG_FEATURES], &z[i * MARS_MAX_REG_FEATURES]);
    }

    st = hmm_fit_on_scaled(h, z, n, k, MARS_MAX_REG_FEATURES);
    if (st != MARS_OK) {
        goto done;
    }
    h->scaler = scaler;

done:
    free(raw);
    free(z);
    return st;
}


static mars_status_t hmm_filter_one(
    const mars_hmm_t *h,
    const double *reg_raw,
    const double *pi_prev,
    double *pi_next)
{
    double x[MARS_MAX_REG_FEATURES];
    double logp[MARS_MAX_STATES];
    double norm;
    uint32_t i;
    uint32_t j;

    if ((h == NULL) || (reg_raw == NULL) || (pi_prev == NULL) || (pi_next == NULL)) {
        return MARS_ERR_ARG;
    }

    ScalerOps::applyVec(&h->scaler, reg_raw, x);

    for (j = 0U; j < h->k; ++j) {
        double s = MARS_LOG_ZERO;
        for (i = 0U; i < h->k; ++i) {
            s = logaddexp_d(s, log(pi_prev[i] + MARS_EPS) + log(h->a[i][j] + MARS_EPS));
        }
        logp[j] = hmm_emit_logp(h, j, x) + s;
    }

    norm = logsumexp_d(logp, h->k);
    if (!isfinite(norm)) {
        return MARS_ERR_NUM;
    }

    for (j = 0U; j < h->k; ++j) {
        pi_next[j] = exp(logp[j] - norm);
    }

    return MARS_OK;
}


mars_status_t HmmModel::filterRange(
    const mars_hmm_t *h,
    const mars_data_t *d,
    size_t start,
    size_t end,
    double *pi_out)
{
    size_t t;
    uint32_t k;
    double pi_prev[MARS_MAX_STATES];
    double pi_next[MARS_MAX_STATES];
    mars_status_t st;

    if ((h == NULL) || (d == NULL) || (pi_out == NULL) || (end <= start)) {
        return MARS_ERR_ARG;
    }

    for (k = 0U; k < h->k; ++k) {
        pi_prev[k] = h->pi[k];
    }

    for (t = start; t < end; ++t) {
        st = hmm_filter_one(h, d->row[t].reg, pi_prev, pi_next);
        if (st != MARS_OK) {
            return st;
        }
        for (k = 0U; k < h->k; ++k) {
            pi_out[((t - start) * h->k) + k] = pi_next[k];
            pi_prev[k] = pi_next[k];
        }
    }

    return MARS_OK;
}


double HmmModel::logLikRange(const mars_hmm_t *h, const mars_data_t *d, size_t start, size_t end)
{
    size_t t;
    uint32_t i;
    uint32_t j;
    double pi_prev[MARS_MAX_STATES];
    double pi_next[MARS_MAX_STATES];
    double ll = 0.0;

    if ((h == NULL) || (d == NULL) || (end <= start)) {
        return -DBL_MAX;
    }

    for (i = 0U; i < h->k; ++i) {
        pi_prev[i] = h->pi[i];
    }

    for (t = start; t < end; ++t) {
        double x[MARS_MAX_REG_FEATURES];
        double logp[MARS_MAX_STATES];
        double norm;

        ScalerOps::applyVec(&h->scaler, d->row[t].reg, x);
        for (j = 0U; j < h->k; ++j) {
            double s = MARS_LOG_ZERO;
            for (i = 0U; i < h->k; ++i) {
                s = logaddexp_d(s, log(pi_prev[i] + MARS_EPS) + log(h->a[i][j] + MARS_EPS));
            }
            logp[j] = hmm_emit_logp(h, j, x) + s;
        }
        norm = logsumexp_d(logp, h->k);
        ll += norm;
        for (j = 0U; j < h->k; ++j) {
            pi_next[j] = exp(logp[j] - norm);
            pi_prev[j] = pi_next[j];
        }
    }

    return ll / (double)(end - start);
}

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "store.hpp"
#include "report.hpp"

void report::print_stats(const char *name, const mars_bt_stats_t *s)
{
    if ((name == NULL) || (s == NULL)) {
        return;
    }

    (void)printf("%s net_ticks=%.6f gross_ticks=%.6f cost_ticks=%.6f "
                 "bar_sharpe=%.8f trades=%.0f turnover=%.2f max_dd_ticks=%.6f\n",
                 name,
                 s->net_pnl_ticks,
                 s->gross_pnl_ticks,
                 s->cost_ticks,
                 s->sharpe_bar,
                 s->trades,
                 s->turnover,
                 s->max_dd_ticks);
}


static void print_reg_feature_name(uint32_t j)
{
    static const char *names[MARS_MAX_REG_FEATURES] = {
        "ret1_ticks",
        "ret5_ticks",
        "abs_ret5_ticks",
        "rv30_ticks",
        "spread_ticks",
        "log_depth",
        "book_imbalance",
        "ofi5_norm"
    };

    if (j < MARS_MAX_REG_FEATURES) {
        (void)printf("%s", names[j]);
    } else {
        (void)printf("feature_%u", j);
    }
}


mars_status_t report::inspect_model(const char *model_path)
{
    mars_model_t m;
    uint32_t i;
    uint32_t j;
    mars_status_t st;

    memset(&m, 0, sizeof(m));
    st = store::load(model_path, &m);
    if (st != MARS_OK) {
        return st;
    }

    (void)printf("model version=%u K=%u horizon=%u tick_size=%.8f\n",
                 m.version, m.k, m.horizon, m.tick_size);
    (void)printf("costs turn=%.6f edge=%.6f buffer=%.6f max_spread=%.6f pos_max=%.2f\n",
                 m.turn_cost_ticks, m.edge_cost_ticks, m.buffer_ticks,
                 m.max_spread_ticks, m.pos_max);
    (void)printf("aug_dim=%u alpha_intercept_ticks=%.10f\n",
                 m.aug_dim, m.alpha_intercept);

    (void)printf("\ntransition_matrix\n");
    for (i = 0U; i < m.k; ++i) {
        for (j = 0U; j < m.k; ++j) {
            (void)printf("%.8f%s", m.hmm.a[i][j], (j + 1U == m.k) ? "\n" : ",");
        }
    }

    (void)printf("\nstate_emission_means_normalized\n");
    for (i = 0U; i < m.k; ++i) {
        (void)printf("state_%u", i);
        for (j = 0U; j < m.hmm.d; ++j) {
            (void)printf(",");
            print_reg_feature_name(j);
            (void)printf("=%.6f", m.hmm.mu[i][j]);
        }
        (void)printf("\n");
    }

    (void)printf("\nnonzero_alpha_coefficients\n");
    for (j = 0U; j < m.aug_dim; ++j) {
        if (fabs(m.beta[j]) > 1.0e-10) {
            (void)printf("beta[%u]=%.10f\n", j, m.beta[j]);
        }
    }

    return MARS_OK;
}


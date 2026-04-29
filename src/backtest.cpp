#include <math.h>
#include <stdio.h>
#include <string.h>

#include "backtest.hpp"

static double dabs(double x)
{
    return (x < 0.0) ? -x : x;
}


static double dmax(double a, double b)
{
    return (a > b) ? a : b;
}


static double dmin(double a, double b)
{
    return (a < b) ? a : b;
}


static double prediction_to_pos(const mars_model_t *m, const mars_row_t *r, double pred_ticks)
{
    const double threshold = m->edge_cost_ticks + m->buffer_ticks;

    if (r->spread_ticks > m->max_spread_ticks) {
        return 0.0;
    }

    if (pred_ticks > threshold) {
        return m->pos_max;
    }
    if (pred_ticks < -threshold) {
        return -m->pos_max;
    }
    return 0.0;
}


mars_bt_stats_t Backtester::evaluate(
    const mars_model_t *m,
    const mars_data_t *d,
    size_t start,
    size_t end,
    const double *pred,
    const char *trades_path)
{
    mars_bt_stats_t s;
    FILE *fp = NULL;
    double pos = 0.0;
    double eq = 0.0;
    double peak = 0.0;
    double ss = 0.0;
    size_t i;
    size_t n = 0U;

    memset(&s, 0, sizeof(s));

    if (trades_path != NULL) {
        fp = fopen(trades_path, "w");
        if (fp != NULL) {
            (void)fprintf(fp, "ts,mid,pred_ticks,pos,target,bar_pnl_ticks,cost_ticks,equity_ticks\n");
        }
    }

    for (i = start + 1U; i < end; ++i) {
        const size_t pidx = i - start;
        const double target = prediction_to_pos(m, &d->row[i - 1U], pred[pidx - 1U]);
        const double turnover = dabs(target - pos);
        const double cost = turnover * m->turn_cost_ticks;
        const double bar = pos * ((d->row[i].mid - d->row[i - 1U].mid) / m->tick_size);
        const double net = bar - cost;

        if (turnover > MARS_EPS) {
            s.trades += 1.0;
            s.turnover += turnover;
        }

        s.gross_pnl_ticks += bar;
        s.cost_ticks += cost;
        s.net_pnl_ticks += net;
        ss += net * net;
        eq += net;
        peak = dmax(peak, eq);
        s.max_dd_ticks = dmin(s.max_dd_ticks, eq - peak);
        ++n;

        if (fp != NULL) {
            (void)fprintf(fp, "%llu,%.10f,%.10f,%.4f,%.4f,%.10f,%.10f,%.10f\n",
                          (unsigned long long)d->row[i].ts,
                          d->row[i].mid,
                          pred[pidx - 1U],
                          pos,
                          target,
                          net,
                          cost,
                          eq);
        }

        pos = target;
    }

    if (fp != NULL) {
        (void)fclose(fp);
    }

    if (n > 1U) {
        const double mean = s.net_pnl_ticks / (double)n;
        const double var = dmax(0.0, (ss / (double)n) - (mean * mean));
        s.mean_pnl_ticks = mean;
        s.sd_pnl_ticks = sqrt(var);
        s.sharpe_bar = mean / (s.sd_pnl_ticks + MARS_EPS);
    }

    return s;
}

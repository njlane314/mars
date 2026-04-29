/*
 * core.cpp -- C++ orchestration behind the mars C ABI.
 */

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "alpha.hpp"
#include "backtest.hpp"
#include "data.hpp"
#include "features.hpp"
#include "hmm.hpp"
#include "api.h"
#include "store.hpp"
#include "report.hpp"

namespace {

class MarsEngine final {
public:
    MarsEngine() = delete;

    static mars_status_t fitDb(const char *db_path, const char *table, const char *model_path)
    {
        mars_data_t d;
        mars_status_t st;

        memset(&d, 0, sizeof(d));
        st = Data::loadBars(db_path, table, &d);
        if (st == MARS_OK) {
            st = fitLoaded(&d, model_path);
        }
        Data::release(&d);
        return st;
    }

    static mars_status_t replayDb(const char *db_path, const char *table,
                                const char *model_path, const char *trades_path)
    {
        mars_data_t d;
        mars_status_t st;

        memset(&d, 0, sizeof(d));
        st = Data::loadBars(db_path, table, &d);
        if (st == MARS_OK) {
            st = replayLoaded(&d, model_path, trades_path);
        }
        Data::release(&d);
        return st;
    }

private:
    static mars_status_t fitLoaded(mars_data_t *d, const char *model_path)
    {
        mars_config_t cfg;
        mars_model_t final_model;
        double best_score = -DBL_MAX;
        double best_lambda = 0.0;
        uint32_t best_k = 0U;
        mars_bt_stats_t best_stats;
        size_t warm;
        size_t end_usable;
        size_t n_usable;
        size_t split1;
        size_t split2;
        size_t final_train_end;
        uint32_t k;
        mars_status_t st;
        const double lambdas[MARS_MAX_LAMBDAS] = {
            1.0e-5, 3.0e-5, 1.0e-4, 3.0e-4, 1.0e-3,
            3.0e-3, 1.0e-2, 3.0e-2, 1.0e-1
        };

        if ((d == NULL) || (d->row == NULL) || (model_path == NULL)) {
            return MARS_ERR_ARG;
        }

        memset(&final_model, 0, sizeof(final_model));
        memset(&best_stats, 0, sizeof(best_stats));
        cfg = ModelStore::defaultConfig();

        st = FeatureBuilder::make(d, &cfg);
        if (st != MARS_OK) {
            return st;
        }

        warm = 180U;
        if (d->n <= warm + cfg.horizon + 100U) {
            return MARS_ERR_STATE;
        }
        end_usable = d->n - cfg.horizon - 2U;
        n_usable = end_usable - warm;
        split1 = warm + (size_t)(0.60 * (double)n_usable);
        split2 = warm + (size_t)(0.80 * (double)n_usable);
        final_train_end = split2;

        (void)printf("rows=%zu usable=[%zu,%zu) train=[%zu,%zu) val=[%zu,%zu) test=[%zu,%zu)\n",
                     d->n, warm, end_usable, warm, split1, split1, split2, split2, end_usable);

        for (k = 1U; k <= MARS_MAX_STATES; ++k) {
            mars_model_t m;
            double hmm_ll;
            uint32_t li;

            st = ModelStore::initFromConfig(&m, &cfg, k);
            if (st != MARS_OK) {
                return st;
            }

            st = HmmModel::fitPreserveScaler(&m.hmm, d, warm, split1, k);
            if (st != MARS_OK) {
                continue;
            }
            hmm_ll = HmmModel::logLikRange(&m.hmm, d, split1, split2);

            for (li = 0U; li < MARS_MAX_LAMBDAS; ++li) {
                mars_model_t mt = m;
                mars_bt_stats_t stv;
                double score;

                st = AlphaModel::trainEval(&mt, d, warm, split1, split1, split2, lambdas[li], &stv);
                if (st != MARS_OK) {
                    continue;
                }

                score = ModelStore::selectScore(&stv);
                (void)printf("candidate K=%u lambda=%.6g hmm_val_ll=%.8f score=%.8f ",
                             k, lambdas[li], hmm_ll, score);
                Reporter::printStats("val", &stv);

                if (score > best_score) {
                    best_score = score;
                    best_lambda = lambdas[li];
                    best_k = k;
                    best_stats = stv;
                }
            }
        }

        if ((best_k == 0U) || (!isfinite(best_score))) {
            return MARS_ERR_STATE;
        }

        (void)printf("selected K=%u lambda=%.6g score=%.8f\n", best_k, best_lambda, best_score);
        Reporter::printStats("selected_validation", &best_stats);

        st = ModelStore::initFromConfig(&final_model, &cfg, best_k);
        if (st != MARS_OK) {
            return st;
        }

        st = HmmModel::fitPreserveScaler(&final_model.hmm, d, warm, final_train_end, best_k);
        if (st != MARS_OK) {
            return st;
        }

        st = AlphaModel::trainFinal(&final_model, d, warm, final_train_end, best_lambda);
        if (st != MARS_OK) {
            return st;
        }

        if ((MARS_REQUIRE_POSITIVE_VALIDATION != 0U) &&
            ((best_stats.net_pnl_ticks <= 0.0) || (best_stats.sharpe_bar <= 0.0))) {
            (void)printf("validation gate failed: saved model is deployment-disabled with pos_max=0\n");
            final_model.pos_max = 0.0;
        }

        {
            double *pred = (double *)calloc(end_usable - final_train_end, sizeof(double));
            if (pred == NULL) {
                return MARS_ERR_MEM;
            }
            st = AlphaModel::predictRange(&final_model, d, final_train_end, end_usable, pred);
            if (st == MARS_OK) {
                mars_bt_stats_t tst = Backtester::evaluate(&final_model, d, final_train_end, end_usable, pred, NULL);
                Reporter::printStats("holdout", &tst);
            }
            free(pred);
            if (st != MARS_OK) {
                return st;
            }
        }

        st = ModelStore::save(model_path, &final_model);
        return st;
    }

    static mars_status_t replayLoaded(mars_data_t *d, const char *model_path, const char *trades_path)
    {
        mars_model_t m;
        mars_config_t cfg;
        double *pred = NULL;
        mars_status_t st;
        size_t warm;
        size_t end_usable;
        mars_bt_stats_t stats;

        if ((d == NULL) || (d->row == NULL) || (model_path == NULL)) {
            return MARS_ERR_ARG;
        }

        memset(&m, 0, sizeof(m));

        st = ModelStore::load(model_path, &m);
        if (st != MARS_OK) {
            return st;
        }

        cfg = ModelStore::defaultConfig();
        cfg.horizon = m.horizon;
        cfg.tick_size = m.tick_size;
        cfg.turn_cost_ticks = m.turn_cost_ticks;
        cfg.edge_cost_ticks = m.edge_cost_ticks;
        cfg.buffer_ticks = m.buffer_ticks;
        cfg.max_spread_ticks = m.max_spread_ticks;
        cfg.pos_max = m.pos_max;

        st = FeatureBuilder::make(d, &cfg);
        if (st != MARS_OK) {
            return st;
        }

        warm = 180U;
        if (d->n <= warm + m.horizon + 100U) {
            return MARS_ERR_STATE;
        }
        end_usable = d->n - m.horizon - 2U;

        pred = (double *)calloc(end_usable - warm, sizeof(double));
        if (pred == NULL) {
            return MARS_ERR_MEM;
        }

        st = AlphaModel::predictRange(&m, d, warm, end_usable, pred);
        if (st != MARS_OK) {
            free(pred);
            return st;
        }

        stats = Backtester::evaluate(&m, d, warm, end_usable, pred, trades_path);
        Reporter::printStats("replay", &stats);

        free(pred);
        return MARS_OK;
    }
};

}

mars_status_t mars_fit_db(const char *db_path, const char *table, const char *model_path)
{
    return MarsEngine::fitDb(db_path, table, model_path);
}

mars_status_t mars_replay_db(const char *db_path, const char *table,
                           const char *model_path, const char *trades_path)
{
    return MarsEngine::replayDb(db_path, table, model_path, trades_path);
}

mars_status_t mars_inspect(const char *model_path)
{
    return Reporter::inspectModel(model_path);
}

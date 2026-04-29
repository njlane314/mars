/*
 * mars_core.cpp -- C++ orchestration behind the mars C ABI.
 */

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "alpha.hpp"
#include "backtest.hpp"
#include "csv.hpp"
#include "features.hpp"
#include "hmm.hpp"
#include "mars_api.h"
#include "model_store.hpp"
#include "report.hpp"

namespace {

class MarsEngine final {
public:
    MarsEngine() = delete;

    static rt_status_t fit(const char *csv_path, const char *model_path)
    {
        rt_data_t d;
        rt_config_t cfg;
        rt_model_t final_model;
        double best_score = -DBL_MAX;
        double best_lambda = 0.0;
        uint32_t best_k = 0U;
        rt_bt_stats_t best_stats;
        size_t warm;
        size_t end_usable;
        size_t n_usable;
        size_t split1;
        size_t split2;
        size_t final_train_end;
        uint32_t k;
        rt_status_t st;
        const double lambdas[RT_MAX_LAMBDAS] = {
            1.0e-5, 3.0e-5, 1.0e-4, 3.0e-4, 1.0e-3,
            3.0e-3, 1.0e-2, 3.0e-2, 1.0e-1
        };

        memset(&d, 0, sizeof(d));
        memset(&final_model, 0, sizeof(final_model));
        memset(&best_stats, 0, sizeof(best_stats));
        cfg = ModelStore::defaultConfig();

        st = CsvData::load(csv_path, &d);
        if (st != RT_OK) {
            return st;
        }

        st = FeatureBuilder::make(&d, &cfg);
        if (st != RT_OK) {
            CsvData::release(&d);
            return st;
        }

        warm = 180U;
        if (d.n <= warm + cfg.horizon + 100U) {
            CsvData::release(&d);
            return RT_ERR_STATE;
        }
        end_usable = d.n - cfg.horizon - 2U;
        n_usable = end_usable - warm;
        split1 = warm + (size_t)(0.60 * (double)n_usable);
        split2 = warm + (size_t)(0.80 * (double)n_usable);
        final_train_end = split2;

        (void)printf("rows=%zu usable=[%zu,%zu) train=[%zu,%zu) val=[%zu,%zu) test=[%zu,%zu)\n",
                     d.n, warm, end_usable, warm, split1, split1, split2, split2, end_usable);

        for (k = 1U; k <= RT_MAX_STATES; ++k) {
            rt_model_t m;
            double hmm_ll;
            uint32_t li;

            st = ModelStore::initFromConfig(&m, &cfg, k);
            if (st != RT_OK) {
                CsvData::release(&d);
                return st;
            }

            st = HmmModel::fitPreserveScaler(&m.hmm, &d, warm, split1, k);
            if (st != RT_OK) {
                continue;
            }
            hmm_ll = HmmModel::logLikRange(&m.hmm, &d, split1, split2);

            for (li = 0U; li < RT_MAX_LAMBDAS; ++li) {
                rt_model_t mt = m;
                rt_bt_stats_t stv;
                double score;

                st = AlphaModel::trainEval(&mt, &d, warm, split1, split1, split2, lambdas[li], &stv);
                if (st != RT_OK) {
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
            CsvData::release(&d);
            return RT_ERR_STATE;
        }

        (void)printf("selected K=%u lambda=%.6g score=%.8f\n", best_k, best_lambda, best_score);
        Reporter::printStats("selected_validation", &best_stats);

        st = ModelStore::initFromConfig(&final_model, &cfg, best_k);
        if (st != RT_OK) {
            CsvData::release(&d);
            return st;
        }

        st = HmmModel::fitPreserveScaler(&final_model.hmm, &d, warm, final_train_end, best_k);
        if (st != RT_OK) {
            CsvData::release(&d);
            return st;
        }

        st = AlphaModel::trainFinal(&final_model, &d, warm, final_train_end, best_lambda);
        if (st != RT_OK) {
            CsvData::release(&d);
            return st;
        }

        if ((RT_REQUIRE_POSITIVE_VALIDATION != 0U) &&
            ((best_stats.net_pnl_ticks <= 0.0) || (best_stats.sharpe_bar <= 0.0))) {
            (void)printf("validation gate failed: saved model is deployment-disabled with pos_max=0\n");
            final_model.pos_max = 0.0;
        }

        {
            double *pred = (double *)calloc(end_usable - final_train_end, sizeof(double));
            if (pred == NULL) {
                CsvData::release(&d);
                return RT_ERR_MEM;
            }
            st = AlphaModel::predictRange(&final_model, &d, final_train_end, end_usable, pred);
            if (st == RT_OK) {
                rt_bt_stats_t tst = Backtester::evaluate(&final_model, &d, final_train_end, end_usable, pred, NULL);
                Reporter::printStats("holdout", &tst);
            }
            free(pred);
            if (st != RT_OK) {
                CsvData::release(&d);
                return st;
            }
        }

        st = ModelStore::save(model_path, &final_model);
        CsvData::release(&d);
        return st;
    }

    static rt_status_t replay(const char *csv_path, const char *model_path, const char *trades_path)
    {
        rt_data_t d;
        rt_model_t m;
        rt_config_t cfg;
        double *pred = NULL;
        rt_status_t st;
        size_t warm;
        size_t end_usable;
        rt_bt_stats_t stats;

        memset(&d, 0, sizeof(d));
        memset(&m, 0, sizeof(m));

        st = ModelStore::load(model_path, &m);
        if (st != RT_OK) {
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

        st = CsvData::load(csv_path, &d);
        if (st != RT_OK) {
            return st;
        }

        st = FeatureBuilder::make(&d, &cfg);
        if (st != RT_OK) {
            CsvData::release(&d);
            return st;
        }

        warm = 180U;
        if (d.n <= warm + m.horizon + 100U) {
            CsvData::release(&d);
            return RT_ERR_STATE;
        }
        end_usable = d.n - m.horizon - 2U;

        pred = (double *)calloc(end_usable - warm, sizeof(double));
        if (pred == NULL) {
            CsvData::release(&d);
            return RT_ERR_MEM;
        }

        st = AlphaModel::predictRange(&m, &d, warm, end_usable, pred);
        if (st != RT_OK) {
            free(pred);
            CsvData::release(&d);
            return st;
        }

        stats = Backtester::evaluate(&m, &d, warm, end_usable, pred, trades_path);
        Reporter::printStats("replay", &stats);

        free(pred);
        CsvData::release(&d);
        return RT_OK;
    }
};

}

rt_status_t mars_fit(const char *csv_path, const char *model_path)
{
    return MarsEngine::fit(csv_path, model_path);
}

rt_status_t mars_replay(const char *csv_path, const char *model_path, const char *trades_path)
{
    return MarsEngine::replay(csv_path, model_path, trades_path);
}

rt_status_t mars_inspect(const char *model_path)
{
    return Reporter::inspectModel(model_path);
}

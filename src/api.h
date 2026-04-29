#ifndef MARS_API_H
#define MARS_API_H

#include <stddef.h>
#include <stdint.h>

#define MARS_VERSION              2U
#define MARS_MAGIC                0x4d415253U  /* "MARS" */

#define MARS_MAX_LINE             512U
#define MARS_MAX_STATES           5U
#define MARS_MAX_REG_FEATURES     8U
#define MARS_MAX_AUX_FEATURES     8U
#define MARS_MAX_BASE_FEATURES    24U
#define MARS_MAX_AUG_FEATURES     192U
#define MARS_MAX_CD_ITERS         250U
#define MARS_MAX_EM_ITERS         80U
#define MARS_MAX_LAMBDAS          9U
#define MARS_MIN_TRAIN_ROWS       2000U

#define MARS_DEFAULT_HORIZON      60U
#define MARS_DEFAULT_TICK_SIZE    0.25
#define MARS_DEFAULT_TURN_COST    0.625
#define MARS_DEFAULT_EDGE_COST    1.25
#define MARS_DEFAULT_BUFFER       0.50
#define MARS_DEFAULT_MAX_SPREAD   3.00
#define MARS_DEFAULT_POS_MAX      1.0
#define MARS_REQUIRE_POSITIVE_VALIDATION 1U
#define MARS_EPS                  1.0e-12
#define MARS_VAR_FLOOR            1.0e-4
#define MARS_LOG_ZERO             (-1.0e300)
#define MARS_ETH_LATEST           UINT64_MAX

typedef enum
{
    MARS_OK = 0,
    MARS_ERR_ARG = -1,
    MARS_ERR_IO = -2,
    MARS_ERR_MEM = -3,
    MARS_ERR_PARSE = -4,
    MARS_ERR_NUM = -5,
    MARS_ERR_STATE = -6
} mars_status_t;

typedef struct
{
    uint64_t ts;
    double bid;
    double ask;
    double bid_sz;
    double ask_sz;
    double volume;

    double mid;
    double log_mid;
    double spread_ticks;
    double depth;
    double imbalance;
    double micro_dev_ticks;
    double ofi1_norm;
    double ofi5_norm;
    double ret1_ticks;
    double label_ticks;

    double aux[MARS_MAX_AUX_FEATURES];
    double reg[MARS_MAX_REG_FEATURES];
    double base[MARS_MAX_BASE_FEATURES];
} mars_row_t;

typedef struct
{
    mars_row_t *row;
    size_t n;
} mars_data_t;

typedef struct
{
    uint32_t d;
    double mean[MARS_MAX_AUG_FEATURES];
    double sd[MARS_MAX_AUG_FEATURES];
} mars_scaler_t;

typedef struct
{
    uint32_t k;
    uint32_t d;
    mars_scaler_t scaler;
    double pi[MARS_MAX_STATES];
    double a[MARS_MAX_STATES][MARS_MAX_STATES];
    double mu[MARS_MAX_STATES][MARS_MAX_REG_FEATURES];
    double var[MARS_MAX_STATES][MARS_MAX_REG_FEATURES];
} mars_hmm_t;

typedef struct
{
    uint32_t magic;
    uint32_t version;
    uint32_t horizon;
    uint32_t k;
    uint32_t base_dim;
    uint32_t aug_dim;

    double tick_size;
    double turn_cost_ticks;
    double edge_cost_ticks;
    double buffer_ticks;
    double max_spread_ticks;
    double pos_max;

    mars_hmm_t hmm;
    mars_scaler_t base_scaler;
    mars_scaler_t aug_scaler;

    double alpha_intercept;
    double beta[MARS_MAX_AUG_FEATURES];
} mars_model_t;

typedef struct
{
    uint32_t horizon;
    double tick_size;
    double turn_cost_ticks;
    double edge_cost_ticks;
    double buffer_ticks;
    double max_spread_ticks;
    double pos_max;
} mars_config_t;

typedef struct
{
    double net_pnl_ticks;
    double gross_pnl_ticks;
    double cost_ticks;
    double mean_pnl_ticks;
    double sd_pnl_ticks;
    double sharpe_bar;
    double trades;
    double turnover;
    double max_dd_ticks;
} mars_bt_stats_t;

#ifdef __cplusplus
extern "C" {
#endif

mars_status_t mars_fit_db(const char *db_path, const char *table, const char *model_path);
mars_status_t mars_replay_db(const char *db_path, const char *table,
                           const char *model_path, const char *trades_path);
mars_status_t mars_inspect(const char *model_path);
mars_status_t mars_db_init(const char *db_path);
mars_status_t mars_db_summary(const char *db_path);
mars_status_t mars_spot_update(const char *db_path, const char *from_utc,
                             const char *to_utc, uint32_t granularity);
mars_status_t mars_eth_update(const char *db_path, const char *rpc_url,
                            uint64_t from_block, uint64_t to_block,
                            uint32_t store_txs);
mars_status_t mars_eth_export(const char *db_path, const char *out_path);
mars_status_t mars_fred_update(const char *db_path, const char *series_list,
                             const char *api_key);
mars_status_t mars_fred_export(const char *db_path, const char *out_path);

#ifdef __cplusplus
}
#endif

#endif

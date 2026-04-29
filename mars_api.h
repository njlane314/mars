#ifndef MARS_API_H
#define MARS_API_H

#include <stddef.h>
#include <stdint.h>

#define RT_VERSION              1U
#define RT_MAGIC                0x4d415253U  /* "MARS" */

#define RT_MAX_LINE             512U
#define RT_MAX_STATES           5U
#define RT_MAX_REG_FEATURES     8U
#define RT_MAX_BASE_FEATURES    16U
#define RT_MAX_AUG_FEATURES     128U
#define RT_MAX_CD_ITERS         250U
#define RT_MAX_EM_ITERS         80U
#define RT_MAX_LAMBDAS          9U
#define RT_MIN_TRAIN_ROWS       2000U

#define RT_DEFAULT_HORIZON      60U
#define RT_DEFAULT_TICK_SIZE    0.25
#define RT_DEFAULT_TURN_COST    0.625
#define RT_DEFAULT_EDGE_COST    1.25
#define RT_DEFAULT_BUFFER       0.50
#define RT_DEFAULT_MAX_SPREAD   3.00
#define RT_DEFAULT_POS_MAX      1.0
#define RT_REQUIRE_POSITIVE_VALIDATION 1U
#define RT_EPS                  1.0e-12
#define RT_VAR_FLOOR            1.0e-4
#define RT_LOG_ZERO             (-1.0e300)
#define RT_ETH_LATEST           UINT64_MAX

typedef enum
{
    RT_OK = 0,
    RT_ERR_ARG = -1,
    RT_ERR_IO = -2,
    RT_ERR_MEM = -3,
    RT_ERR_PARSE = -4,
    RT_ERR_NUM = -5,
    RT_ERR_STATE = -6
} rt_status_t;

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

    double reg[RT_MAX_REG_FEATURES];
    double base[RT_MAX_BASE_FEATURES];
} rt_row_t;

typedef struct
{
    rt_row_t *row;
    size_t n;
} rt_data_t;

typedef struct
{
    uint32_t d;
    double mean[RT_MAX_AUG_FEATURES];
    double sd[RT_MAX_AUG_FEATURES];
} rt_scaler_t;

typedef struct
{
    uint32_t k;
    uint32_t d;
    rt_scaler_t scaler;
    double pi[RT_MAX_STATES];
    double a[RT_MAX_STATES][RT_MAX_STATES];
    double mu[RT_MAX_STATES][RT_MAX_REG_FEATURES];
    double var[RT_MAX_STATES][RT_MAX_REG_FEATURES];
} rt_hmm_t;

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

    rt_hmm_t hmm;
    rt_scaler_t base_scaler;
    rt_scaler_t aug_scaler;

    double alpha_intercept;
    double beta[RT_MAX_AUG_FEATURES];
} rt_model_t;

typedef struct
{
    uint32_t horizon;
    double tick_size;
    double turn_cost_ticks;
    double edge_cost_ticks;
    double buffer_ticks;
    double max_spread_ticks;
    double pos_max;
} rt_config_t;

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
} rt_bt_stats_t;

#ifdef __cplusplus
extern "C" {
#endif

rt_status_t mars_fit(const char *csv_path, const char *model_path);
rt_status_t mars_replay(const char *csv_path, const char *model_path, const char *trades_path);
rt_status_t mars_inspect(const char *model_path);
rt_status_t mars_db_init(const char *db_path);
rt_status_t mars_eth_update(const char *db_path, const char *rpc_url,
                            uint64_t from_block, uint64_t to_block,
                            uint32_t store_txs);
rt_status_t mars_eth_export(const char *db_path, const char *csv_path);
rt_status_t mars_fred_update(const char *db_path, const char *series_csv,
                             const char *api_key);
rt_status_t mars_fred_export(const char *db_path, const char *csv_path);

#ifdef __cplusplus
}
#endif

#endif

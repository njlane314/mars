#ifndef BACKTEST_HPP
#define BACKTEST_HPP

#include "mars_api.h"

class Backtester final {
public:
    Backtester() = delete;

    static rt_bt_stats_t evaluate(const rt_model_t *model, const rt_data_t *data,
                                  size_t start, size_t end, const double *pred,
                                  const char *trades_path);
};

#endif
